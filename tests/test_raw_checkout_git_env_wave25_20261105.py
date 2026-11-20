"""Source-local Git checkout preflight negative tests, not an AttoLang guest run."""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "raw_checkout", Path(__file__).resolve().parents[1] / "scripts/verify_raw_checkout.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

class RawCheckoutEnvironmentAdmission(unittest.TestCase):
    def test_untrusted_git_environment_never_redirects_checkout_admission(self):
        with patch.dict(os.environ, {}, clear=True):
            checker.validate_git_environment()
            os.environ["GIT_NO_REPLACE_OBJECTS"] = "1"
            os.environ["GIT_TERMINAL_PROMPT"] = "0"
            checker.validate_git_environment()
            for key in ("GIT_INDEX_FILE", "GIT_WORK_TREE", "GIT_DIR",
                        "GIT_OBJECT_DIRECTORY", "GIT_CONFIG_COUNT",
                        "GIT_ALTERNATE_OBJECT_DIRECTORIES"):
                os.environ[key] = "override"
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, "Git control environment"):
                    checker.validate_git_environment()
                del os.environ[key]

    def test_git_subprocess_does_not_forward_inherited_control_variables(self):
        env = {"GIT_INDEX_FILE":"/tmp/alternate", "GIT_CONFIG_COUNT":"1",
               "GIT_NO_REPLACE_OBJECTS":"0", "SAFE_VALUE":"preserved"}
        with patch.dict(os.environ, env, clear=True):
            with patch.object(checker.subprocess, "check_output", return_value=b"ok") as run:
                self.assertEqual(checker.git("rev-parse", "HEAD"), b"ok")
        args, kwargs = run.call_args
        self.assertEqual(args[0][:4], ["git", "-c", "core.fsmonitor=false", "--no-optional-locks"])
        self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
        self.assertEqual(kwargs["env"]["SAFE_VALUE"], "preserved")
        self.assertNotIn("GIT_INDEX_FILE", kwargs["env"])
        self.assertNotIn("GIT_CONFIG_COUNT", kwargs["env"])

    def test_nonportable_and_colliding_git_paths_are_rejected(self):
        valid = [b"src/module.atto", "src/café.atto".encode("utf-8"), b"A/b.txt"]
        bad = [b"", b"/absolute", b"../escape", b"src//f",
               b"src/./file", b"src/../file", b".git/config", b"src/.GIT/config",
               b"COM1.txt", b"CONIN$", b"src/file.", b"file ", b"foo:bar",
               b"src\\evil", b"bad\xff", "cafe\u0301".encode("utf-8")]
        for path in valid:
            with self.subTest(path=path):
                self.assertTrue(checker.safe_path(path))
        for path in bad:
            with self.subTest(path=path):
                self.assertFalse(checker.safe_path(path))

    def test_case_colliding_git_tree_prefixes_are_never_admitted(self):
        oid = b"a" * 40
        row = lambda name: b"100644 blob " + oid + b"\t" + name + b"\0"
        for entries in ((b"A/first", b"a/second"),
                        (b"safe/Node.atto", b"safe/node.atto")):
            with self.subTest(entries=entries):
                with patch.object(checker, "git", return_value=b"".join(map(row, entries))):
                    with self.assertRaisesRegex(ValueError, "colliding"):
                        checker.tree_entries()

if __name__ == "__main__":
    unittest.main()
