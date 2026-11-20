"""Host-only process-environment regressions for the raw-checkout verifier.

These cases exercise the actual verifier helper, not GitHub Actions admission
or AttoLang guest/compiler execution.
"""
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_raw_checkout as admission


class GitProcessEnvironmentScrubTests(unittest.TestCase):
    def test_every_inherited_git_control_fails_before_a_git_spawn(self):
        hostile = (
            "GIT_CONFIG_COUNT",
            "GIT_CONFIG_PARAMETERS",
            "GIT_CONFIG_GLOBAL",
            "GIT_OBJECT_DIRECTORY",
            "GIT_ALTERNATE_OBJECT_DIRECTORIES",
            "GIT_COMMON_DIR",
            "GIT_DIR",
            "GIT_WORK_TREE",
            "GIT_INDEX_FILE",
            "GIT_TRACE2_EVENT",
            "GIT_NAMESPACE",
        )
        for name in hostile:
            with self.subTest(name=name), patch.dict(os.environ, {name: "attacker"}, clear=True):
                with patch.object(admission.subprocess, "check_output") as child:
                    with self.assertRaisesRegex(ValueError, "inherited Git control"):
                        admission.validate_git_environment()
                    child.assert_not_called()

    def test_even_permitted_parent_git_knobs_are_not_forwarded(self):
        inherited = {
            "GIT_NO_REPLACE_OBJECTS": "0",
            "GIT_TERMINAL_PROMPT": "1",
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "core.fsmonitor",
            "GIT_CONFIG_VALUE_0": "true",
            "HOME": "/tmp/test-home",
            "PATH": "/usr/bin",
        }
        with patch.dict(os.environ, inherited, clear=True):
            with patch.object(admission.subprocess, "check_output", return_value=b"ok") as child:
                self.assertEqual(admission.git("rev-parse", "HEAD"), b"ok")
                child.assert_called_once()
                args, kwargs = child.call_args
                self.assertEqual(args[0][:5], [
                    "git", "-c", "core.fsmonitor=false",
                    "--no-optional-locks", "rev-parse"
                ])
                self.assertEqual(args[0][-1], "HEAD")
                self.assertEqual(kwargs["stderr"], subprocess.DEVNULL)
                self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
                self.assertEqual(kwargs["env"]["HOME"], inherited["HOME"])
                self.assertEqual(kwargs["env"]["PATH"], inherited["PATH"])
                self.assertFalse(any(name.startswith("GIT_") and name != "GIT_NO_REPLACE_OBJECTS"
                                     for name in kwargs["env"]))

    def test_only_two_explicit_git_environment_keys_pass_admission(self):
        with patch.dict(os.environ, {
            "GIT_NO_REPLACE_OBJECTS": "0",
            "GIT_TERMINAL_PROMPT": "0",
            "PATH": "/usr/bin",
        }, clear=True):
            admission.validate_git_environment()
        with patch.dict(os.environ, {"GIT_TERMINAL_PROMPT_EXTRA": "0"}, clear=True):
            with self.assertRaises(ValueError):
                admission.validate_git_environment()


if __name__ == "__main__":
    unittest.main()
