import os
import subprocess
from dataclasses import dataclass
from typing import Callable, Optional
import tempfile
from shutil import copy2
import psutil
from signal import SIGINT, Signals
from time import sleep
from concurrent.futures import ThreadPoolExecutor

import pytest


@dataclass
class Intervention:
    func: Callable[[int], None]
    delay: float


@dataclass
class Case:
    commands: list[str]
    exp_stdout: str
    exp_stderr: str = ""
    timeout: Optional[float] = None
    intervention: Optional[Intervention] = None


class Helper:
    @staticmethod
    def chain_funcs(*funcs: Callable[[int], None]):
        def _chained(pid: int):
            for f in funcs:
                f(pid)

        return _chained

    @staticmethod
    def send_signal(sig: Signals):
        def _send_signal(pid: int):
            shell_proc = psutil.Process(pid)
            children = shell_proc.children()
            for child in children:
                child.send_signal(sig)

        return _send_signal

    @staticmethod
    def get_child_procs(pid: int):
        shell_proc = psutil.Process(pid)
        return shell_proc.children()

    @staticmethod
    def assert_proc_alive(name: str):
        def _assert_proc_alive(pid: int):
            assert any([proc.name() == name for proc in psutil.process_iter()])

        return _assert_proc_alive


class TestShell:
    test_cases = [
        pytest.param(Case(["echo hello world"], "hello world\n"), id="sanity - echo"),
        pytest.param(
            Case(["echo hello world | cat"], "hello world\n"), id="echo pipe cat"
        ),
        pytest.param(
            Case(
                ["cat thisfiledoesnotexist.noext"],
                "",
                "cat: can't open 'thisfiledoesnotexist.noext': No such file or directory\n",
            ),
            id="cat with error",
        ),
        pytest.param(
            Case(
                [
                    "echo hello > testfile.txt",
                    "sudo cat testfile.txt",
                    "rm testfile.txt",
                ],
                "hello\n",
            ),
            id="redirect echo to file",
        ),
        pytest.param(
            Case(
                ["sleep 60"],
                "",
                intervention=Intervention(
                    func=Helper.send_signal(SIGINT),
                    delay=1,
                ),
                timeout=3,
            ),
            id="foreground command respects SIGINT",
        ),
        pytest.param(
            Case(
                ["sleep 3 &"],
                "",
                intervention=Intervention(
                    func=Helper.chain_funcs(
                        Helper.send_signal(SIGINT), Helper.assert_proc_alive("sleep")
                    ),
                    delay=0.3,
                ),
                timeout=5,
            ),
            id="background command does not respect SIGINT",
        ),
    ]

    def run_shell_command(
        self,
        command: str,
        timeout: Optional[float],
        cwd: Optional[str] = None,
        intervention: Optional[Intervention] = None,
    ):
        assert subprocess.call(["sh", "comp.sh"]) == 0
        if cwd:
            copy2("./a.out", os.path.join(cwd, "./a.out"))
        shell = subprocess.Popen(
            ["./a.out"],
            text=True,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
        )
        with ThreadPoolExecutor() as p:
            intervention_task = p.submit(self.intervene, shell.pid, intervention)
            stdout, stderr = shell.communicate(command, timeout=timeout)
            intervention_task.result()
        return stdout, stderr

    def intervene(self, pid: int, intervention: Optional[Intervention]):
        if intervention is None:
            return
        sleep(intervention.delay)
        return intervention.func(pid)

    @pytest.mark.parametrize("test_case", test_cases)
    def test_shell_commands(self, test_case: Case):
        with tempfile.TemporaryDirectory() as tdir:
            try:
                stdout, stderr = self.run_shell_command(
                    "\n".join(test_case.commands),
                    test_case.timeout,
                    cwd=tdir,
                    intervention=test_case.intervention,
                )
            except subprocess.TimeoutExpired:
                assert (
                    False
                ), f"shell took longer than expected ({test_case.timeout}s) to run commands: {test_case.commands}"
        assert stderr == test_case.exp_stderr
        assert stdout == test_case.exp_stdout

    def test_redirect_to_file_command(self):
        with tempfile.NamedTemporaryFile() as f:
            stdout, stderr = self.run_shell_command(
                f"echo hello world > {f.name}", None
            )
            with open(f.name, "r") as f_cont:
                assert stderr == ""
                assert f_cont.read() == "hello world\n"
                assert stdout == ""

    def test_background_command(self):
        stdout, stderr = self.run_shell_command("date +%s\nsleep 2 &\ndate +%s", None)
        timestamps = stdout.split("\n")
        assert not stderr
        assert (
            timestamps[0] == timestamps[1]
        ), "sleep command seems to have been blocking"
