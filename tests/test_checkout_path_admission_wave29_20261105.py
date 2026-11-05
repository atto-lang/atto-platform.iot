"""Host-only portable path and inherited Git-control checks."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_raw_checkout import safe_path, validate_git_environment


class RawCheckoutPathAdmissionTests(unittest.TestCase):
    def test_nested_git_metadata_and_device_aliases_are_rejected(self):
        for path in (b".git/config", b"lib/.GIT/config", b"dir/../file",
                     b"dir/./file", b"dir//file", b"dir\\file",
                     b"ports/CONIN$", b"ports/COM9.txt", b"ports/LPT2"):
            with self.subTest(path=path):
                self.assertFalse(safe_path(path))

    def test_ordinary_higher_numbered_ports_remain_portable(self):
        for path in (b"ports/COM10.txt", b"ports/LPT10.txt",
                     b"libs/core.atto", "names/café.atto".encode("utf-8")):
            with self.subTest(path=path):
                self.assertTrue(safe_path(path))

    def test_untrusted_git_index_routing_cannot_be_inherited(self):
        with patch.dict(os.environ, {"GIT_INDEX_FILE": "/tmp/attacker-index"}, clear=True):
            with self.assertRaisesRegex(ValueError, "inherited Git control"):
                validate_git_environment()

    def test_only_named_safe_git_environment_keys_are_admitted(self):
        with patch.dict(os.environ, {"GIT_NO_REPLACE_OBJECTS": "1",
                                     "GIT_TERMINAL_PROMPT": "0"}, clear=True):
            validate_git_environment()


if __name__ == "__main__":
    unittest.main()
