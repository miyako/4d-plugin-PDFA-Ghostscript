import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml


REPO = Path(__file__).resolve().parents[2]
ROOT = Path(os.environ.get("TEST_SOURCE_ROOT", REPO))
spec = importlib.util.spec_from_file_location("release", ROOT / ".github/scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
SHA = "a" * 40


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "PDFA").mkdir()
        self.write_version("7.8.9")

    def write_version(self, version, bundle=None):
        (self.root / "VERSION").write_text(version + "\n")
        # Match the source layout; bump must preserve unrelated plist content.
        (self.root / "PDFA/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleVersion": version if bundle is None else bundle,
            "CFBundleName": "PDFA",
        }))

    def test_derivation_and_all_bumps(self):
        for mode, expected in (("patch", "7.8.10"), ("minor", "7.9.0"), ("major", "8.0.0")):
            with self.subTest(mode=mode):
                self.write_version("7.8.9")
                self.assertEqual(release.source_version(self.root), "7.8.9")
                self.assertEqual(release.bump(self.root, mode), expected)
                self.assertEqual(plistlib.loads((self.root / "PDFA/Info.plist").read_bytes())[
                    "CFBundleName"], "PDFA")

    def test_invalid_versions(self):
        for version in ("", "1.2", "v1.2.3", "01.2.3", "1.02.3", "1.2.03",
                        "1.2.3-beta", "1.2.3+4", "1.2.3 ", "1.2.3\n", "１.2.3"):
            with self.subTest(version=version):
                self.write_version(version)
                with self.assertRaises(ValueError):
                    release.source_version(self.root)

    def test_drift_fails_before_writes(self):
        self.write_version("7.8.9", "7.8.8")
        before = (self.root / "VERSION").read_bytes()
        with self.assertRaisesRegex(ValueError, "match"):
            release.bump(self.root, "patch")
        self.assertEqual((self.root / "VERSION").read_bytes(), before)

    def test_live_consistency(self):
        expected = (ROOT / "VERSION").read_text().strip()
        self.assertEqual(release.source_version(ROOT), expected)

    def test_suite_survives_temporary_copy_bump(self):
        if os.environ.get("TEST_SOURCE_ROOT"):
            return
        copy = self.root / "copy"
        for name in ("VERSION", "PDFA/Info.plist", ".github/scripts/release.py",
                     ".github/workflows/release.yml", ".github/workflows/bump-version.yml"):
            target = copy / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO / name, target)
        original = release.source_version(copy)
        release.bump(copy, "patch")
        self.assertNotEqual(release.source_version(copy), original)
        result = subprocess.run(
            [sys.executable, "-m", "unittest", "discover", "-s", str(REPO / ".github/tests"), "-v"],
            env={**os.environ, "TEST_SOURCE_ROOT": str(copy)}, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def api_result(status, code=1):
    return subprocess.CompletedProcess([], code, f"HTTP/2.0 {status} Test\n\n{{}}\n", "API error")


class AvailabilityTests(unittest.TestCase):
    @patch.object(release.subprocess, "run")
    def test_only_confirmed_missing_tag_and_release_are_available(self, run):
        run.side_effect = [api_result(404), api_result(404)]
        release.check_available("owner/repo", "v7.8.9")
        self.assertEqual(run.call_count, 2)
        self.assertIn("repos/owner/repo/git/ref/tags/v7.8.9", run.call_args_list[0].args[0])
        self.assertIn("repos/owner/repo/releases/tags/v7.8.9", run.call_args_list[1].args[0])

    @patch.object(release.subprocess, "run")
    def test_existing_tag_or_release(self, run):
        for results in ([api_result(200, 0)], [api_result(404), api_result(200, 0)]):
            run.side_effect = results
            with self.assertRaisesRegex(ValueError, "already exists"):
                release.check_available("owner/repo", "v7.8.9")

    @patch.object(release.subprocess, "run")
    def test_api_errors_fail_closed_on_either_lookup(self, run):
        for error in (api_result(401), api_result(403), api_result(429), api_result(500),
                      subprocess.CompletedProcess([], 1, "", "network failed"),
                      subprocess.CompletedProcess([], 0, "invalid response", ""),
                      api_result(404, 0)):
            for prefix in ([], [api_result(404)]):
                run.side_effect = prefix + [error]
                with self.assertRaisesRegex(RuntimeError, "Cannot establish"):
                    release.check_available("owner/repo", "v7.8.9")


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.asset = Path(self.temp.name) / "PDFA.zip"
        self.asset.write_bytes(b"package")

    @patch.object(release, "publish")
    def test_publication_cli_passes_selected_sha_and_assets(self, publish):
        argv = ["release.py", "publish", "--root", str(ROOT), "--sha", SHA, str(self.asset)]
        with patch.object(sys, "argv", argv), patch.dict(os.environ, {"GITHUB_REPOSITORY": "owner/repo"}):
            with contextlib.redirect_stdout(io.StringIO()):
                release.main()
        publish.assert_called_once_with(ROOT, "owner/repo", SHA, [str(self.asset)])

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value=SHA + "\n")
    @patch.object(release, "check_available")
    def test_recheck_then_nonforce_tag_exact_sha_then_create(self, available, head, run):
        events = []
        available.side_effect = lambda *args: events.append("check")
        run.side_effect = lambda command, **kwargs: events.append(command)
        release.publish(ROOT, "owner/repo", SHA, [str(self.asset)])
        tag = "v" + release.source_version(ROOT)
        self.assertEqual(events[0], "check")
        self.assertEqual(events[1], ["git", "tag", tag, SHA])
        self.assertEqual(events[2], ["git", "push", "--no-follow-tags", "origin",
                                     f"refs/tags/{tag}:refs/tags/{tag}"])
        self.assertEqual(events[3], ["gh", "api", "--method", "POST",
                                     "repos/owner/repo/releases", "--input", "-"])
        self.assertEqual(json.loads(run.call_args_list[2].kwargs["input"]), {
            "tag_name": tag, "target_commitish": SHA, "name": tag,
            "draft": False, "prerelease": False, "generate_release_notes": True,
        })
        self.assertEqual(events[4], ["gh", "release", "upload", tag, "--repo",
                                     "owner/repo", str(self.asset)])
        self.assertNotIn("--clobber", events[4])

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value=SHA + "\n")
    @patch.object(release, "check_available", side_effect=ValueError("exists"))
    def test_failed_recheck_has_no_mutation(self, available, head, run):
        with self.assertRaises(ValueError):
            release.publish(ROOT, "owner/repo", SHA, [str(self.asset)])
        run.assert_not_called()

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value=SHA + "\n")
    @patch.object(release, "check_available")
    def test_push_race_stops_release_creation(self, available, head, run):
        run.side_effect = [None, subprocess.CalledProcessError(1, "git push")]
        with self.assertRaises(subprocess.CalledProcessError):
            release.publish(ROOT, "owner/repo", SHA, [str(self.asset)])
        self.assertEqual(run.call_count, 2)

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value=SHA + "\n")
    @patch.object(release, "check_available")
    def test_partial_failure_is_explicit_and_never_deletes(self, available, head, run):
        for successful_calls in (2, 3):
            with self.subTest(successful_calls=successful_calls):
                run.reset_mock()
                run.side_effect = [None] * successful_calls + [
                    subprocess.CalledProcessError(1, "publication failure")]
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(subprocess.CalledProcessError):
                    release.publish(ROOT, "owner/repo", SHA, [str(self.asset)])
                self.assertIn("was pushed", stderr.getvalue())
                self.assertIn("incomplete assets", stderr.getvalue())
                self.assertEqual(run.call_count, successful_calls + 1)
                self.assert_no_destructive_commands(run)

    def assert_no_destructive_commands(self, run):
        for call in run.call_args_list:
            command = call.args[0]
            for forbidden in ("DELETE", "PATCH", "PUT", "--clobber", "--force",
                              "--delete", "delete", "edit", "create"):
                self.assertNotIn(forbidden, command)

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value=SHA + "\n")
    @patch.object(release, "check_available")
    def test_api_timeout_or_partial_upload_preserves_remote_state(self, available, head, run):
        for failure in ("create", "upload"):
            with self.subTest(failure=failure):
                state = {"tag": False, "release": False, "assets": []}

                def command(args, **kwargs):
                    if args[:2] == ["git", "push"]:
                        state["tag"] = True
                    elif args[:2] == ["gh", "api"]:
                        state["release"] = True
                        if failure == "create":
                            raise subprocess.CalledProcessError(1, args)
                    elif args[:3] == ["gh", "release", "upload"]:
                        state["assets"].append(self.asset.name)
                        raise subprocess.CalledProcessError(1, args)

                run.reset_mock()
                run.side_effect = command
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(subprocess.CalledProcessError):
                    release.publish(ROOT, "owner/repo", SHA, [str(self.asset)])
                self.assertTrue(state["tag"])
                self.assertTrue(state["release"])
                self.assertEqual(state["assets"], [] if failure == "create" else [self.asset.name])
                self.assert_no_destructive_commands(run)

    @patch.object(release.subprocess, "run")
    @patch.object(release.subprocess, "check_output", return_value="b" * 40)
    def test_moving_head_invalid_sha_and_missing_assets_fail(self, head, run):
        for sha in ("main", SHA):
            with self.assertRaises(ValueError):
                release.publish(ROOT, "owner/repo", sha, [str(self.asset)])
        head.return_value = SHA
        for assets in ([], [str(self.asset) + ".missing"]):
            with self.assertRaises(ValueError):
                release.publish(ROOT, "owner/repo", SHA, assets)
        run.assert_not_called()


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        # BaseLoader keeps GitHub's "on" key a string rather than YAML 1.1 true.
        self.build = yaml.load((ROOT / ".github/workflows/release.yml").read_text(),
                               Loader=yaml.BaseLoader)
        self.bump = yaml.load((ROOT / ".github/workflows/bump-version.yml").read_text(),
                              Loader=yaml.BaseLoader)

    def test_manual_no_input_and_global_serialization(self):
        self.assertEqual(self.build["name"], "Build")
        self.assertEqual(self.build["on"], {"workflow_dispatch": ""})
        self.assertEqual(self.build["concurrency"],
                         {"group": "release-publication", "cancel-in-progress": "false"})
        for job in self.build["jobs"].values():
            self.assertNotIn("main", job.get("if", ""))
            self.assertNotIn("environment", job)

    def test_all_build_checkouts_are_immutable(self):
        self.assertEqual(set(self.build["jobs"]), {"version", "build-windows", "build-macos"})
        for job in self.build["jobs"].values():
            checkouts = [s for s in job["steps"] if s.get("uses", "").startswith("actions/checkout@")]
            self.assertEqual(len(checkouts), 1)
            self.assertEqual(checkouts[0]["with"]["ref"], "${{ github.sha }}")
        self.assertEqual(self.build["jobs"]["build-windows"]["needs"], "version")
        self.assertEqual(self.build["jobs"]["build-macos"]["needs"], ["version", "build-windows"])

    def test_publish_is_last_and_after_every_gate(self):
        steps = self.build["jobs"]["build-macos"]["steps"]
        names = [s.get("name", "") for s in steps]
        self.assertIn("release.py publish", steps[-1]["run"])
        self.assertEqual(steps[-1]["env"]["SELECTED_SHA"], "${{ github.sha }}")
        for name in ("Verify merged bundle and bundled Ghostscript", "Codesign all binaries",
                     "Package, notarize & staple", "Upload verified release packages"):
            self.assertLess(names.index(name), len(steps) - 1)
        self.assertIn("codesign --verify", steps[names.index("Codesign all binaries")]["run"])
        self.assertIn("stapler validate", steps[names.index("Package, notarize & staple")]["run"])
        self.assertIn('== "Accepted"', steps[names.index("Package, notarize & staple")]["run"])
        for step in steps:
            self.assertNotIn("continue-on-error", step)
            self.assertNotIn("if", step)
        self.assertNotIn("if", self.build["jobs"]["build-macos"])

    def test_bump_metadata_only_selected_branch(self):
        self.assertEqual(self.bump["on"]["workflow_dispatch"]["inputs"]["bump"]["options"],
                         ["patch", "minor", "major"])
        self.assertEqual(self.bump["concurrency"]["group"], "bump-version-${{ github.ref }}")
        self.assertEqual(self.bump["concurrency"]["cancel-in-progress"], "false")
        steps = self.bump["jobs"]["bump"]["steps"]
        self.assertEqual(steps[0]["with"]["ref"], "${{ github.sha }}")
        body = "\n".join(step.get("run", "") for step in steps)
        self.assertIn("git add VERSION PDFA/Info.plist", body)
        self.assertIn('git push --no-follow-tags origin "HEAD:refs/heads/$BRANCH"', body)
        for forbidden in ("git tag", "--tags", "--force", "gh release create"):
            self.assertNotIn(forbidden, body)


if __name__ == "__main__":
    unittest.main()
