"""sync_skill.py: the copy of the tps25751-pd-verify skill for a downstream Codex project."""

import importlib.util
import json
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "sync_skill.py"
spec = importlib.util.spec_from_file_location("sync_skill", SCRIPT)
sync_skill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync_skill)

ORIGIN = {"library_version": "9.9.9", "commit": "0" * 40, "describe": "v9.9.9"}


def fake_library(root: Path, files: dict[str, str]) -> Path:
    """A library tree with the skill at plugins/tps25751-bench/skills/tps25751-pd-verify."""
    source = root / "plugins" / "tps25751-bench" / "skills" / sync_skill.SKILL_NAME
    for name, text in files.items():
        (source / name).parent.mkdir(parents=True, exist_ok=True)
        (source / name).write_text(text, encoding="utf-8")
    (root / "library.json").write_text(json.dumps({"version": "9.9.9"}), encoding="utf-8")
    return source


@pytest.fixture
def library(tmp_path):
    return fake_library(tmp_path / "lib", {"SKILL.md": "---\nname: tps25751-pd-verify\n---\n",
                                           "compare-rules.md": "rules\n"})


@pytest.fixture
def project(tmp_path):
    path = tmp_path / "project"
    path.mkdir()
    return path


def target(project):
    return project / ".agents" / "skills" / sync_skill.SKILL_NAME


def test_copies_the_skill_and_records_its_origin(library, project):
    result = sync_skill.sync(project, library, origin=ORIGIN)
    copy = target(project)
    assert result["files"] == ["SKILL.md", "compare-rules.md"]
    assert (copy / "SKILL.md").read_text() == (library / "SKILL.md").read_text()
    assert not copy.is_symlink()
    marker = json.loads((copy / sync_skill.MARKER).read_text())
    assert marker["format"] == "tps25751-skill-sync" and marker["origin"] == ORIGIN
    assert set(marker["files"]) == {"SKILL.md", "compare-rules.md"}
    assert not list(copy.parent.glob(".*sync-*"))  # no staging directory left behind


def test_a_second_sync_replaces_the_whole_copy(library, project):
    sync_skill.sync(project, library, origin=ORIGIN)
    (library / "compare-rules.md").unlink()
    (library / "SKILL.md").write_text("new\n")
    sync_skill.sync(project, library, origin=ORIGIN)
    assert sorted(p.name for p in target(project).iterdir()) == [sync_skill.MARKER, "SKILL.md"]
    assert (target(project) / "SKILL.md").read_text() == "new\n"


def test_subdirectories_are_copied_and_junk_is_not(project, tmp_path):
    source = fake_library(tmp_path / "lib2", {"SKILL.md": "s", "ref/a.md": "a",
                                              "__pycache__/x.pyc": "x", ".DS_Store": "d"})
    assert sync_skill.sync(project, source, origin=ORIGIN)["files"] == ["SKILL.md", "ref/a.md"]
    assert (target(project) / "ref" / "a.md").read_text() == "a"


def test_a_directory_written_by_hand_is_left_alone(library, project):
    target(project).mkdir(parents=True)
    (target(project) / "SKILL.md").write_text("mine\n")
    with pytest.raises(sync_skill.SyncError, match="not written by this script"):
        sync_skill.sync(project, library, origin=ORIGIN)
    assert (target(project) / "SKILL.md").read_text() == "mine\n"
    sync_skill.sync(project, library, force=True, origin=ORIGIN)
    assert (target(project) / sync_skill.MARKER).is_file()


def test_a_symlink_is_left_alone_without_force(library, project):
    target(project).parent.mkdir(parents=True)
    target(project).symlink_to(library)
    with pytest.raises(sync_skill.SyncError, match="is a symlink"):
        sync_skill.sync(project, library, origin=ORIGIN)
    sync_skill.sync(project, library, force=True, origin=ORIGIN)
    assert not target(project).is_symlink()
    assert (library / "SKILL.md").is_file()  # replacing the link did not touch what it pointed at


def test_the_library_itself_is_refused(library):
    with pytest.raises(sync_skill.SyncError, match="the library itself"):
        sync_skill.sync(library.parents[3], library, origin=ORIGIN)


@pytest.mark.parametrize("linked", [".agents", ".agents/skills"])
def test_a_link_into_the_library_is_refused_even_with_force(library, project, linked):
    # A project whose .agents or .agents/skills links into the library would have the copy
    # replace the library's own files.
    link = project / linked
    link.parent.mkdir(parents=True, exist_ok=True)
    link.symlink_to(library.parents[3] / linked if linked == ".agents" else library.parent)
    (library.parents[3] / ".agents").mkdir(exist_ok=True)
    with pytest.raises(sync_skill.SyncError, match="resolves into the library"):
        sync_skill.sync(project, library, force=True, origin=ORIGIN)
    assert (library / "SKILL.md").is_file()


def test_a_failed_rename_keeps_the_old_copy(library, project, monkeypatch):
    sync_skill.sync(project, library, origin=ORIGIN)
    (library / "SKILL.md").write_text("new\n")
    real_rename = Path.rename

    def failing_rename(self, target):
        if self.name.startswith(f".{sync_skill.SKILL_NAME}.sync-"):
            raise OSError("simulated failure")
        return real_rename(self, target)

    monkeypatch.setattr(Path, "rename", failing_rename)
    with pytest.raises(OSError, match="simulated"):
        sync_skill.sync(project, library, origin=ORIGIN)
    monkeypatch.undo()
    assert (target(project) / "SKILL.md").read_text().startswith("---")  # the old copy
    assert sorted(p.name for p in target(project).parent.iterdir()) == [sync_skill.SKILL_NAME]


def test_a_missing_project_or_skill_is_refused(library, tmp_path):
    with pytest.raises(sync_skill.SyncError, match="not a directory"):
        sync_skill.sync(tmp_path / "nowhere", library, origin=ORIGIN)
    with pytest.raises(sync_skill.SyncError, match="no SKILL.md"):
        sync_skill.sync(tmp_path, tmp_path / "empty", origin=ORIGIN)


def test_dry_run_writes_nothing(library, project, capsys):
    assert sync_skill.main(["--dest", str(project), "--dry-run"], source=library) == 0
    assert "would write 2 files" in capsys.readouterr().out
    assert not (project / ".agents").exists()


def test_the_command_line_reports_a_refusal(library, project, capsys):
    target(project).mkdir(parents=True)
    assert sync_skill.main(["--dest", str(project)], source=library) == 2
    assert "pass --force" in capsys.readouterr().err


def test_the_real_skill_is_where_the_script_looks():
    skill = sync_skill.SOURCE / "SKILL.md"
    assert skill.is_file()
    assert skill.read_text(encoding="utf-8").startswith("---\nname: tps25751-pd-verify\n")
    # The library's own Codex entry is a relative link to the same directory.
    link = sync_skill.LIBRARY_ROOT / ".agents" / "skills" / sync_skill.SKILL_NAME
    assert link.is_symlink() and not Path(link.readlink()).is_absolute()
    assert link.resolve() == sync_skill.SOURCE.resolve()
    origin = sync_skill.library_origin(sync_skill.LIBRARY_ROOT)
    assert origin["library_version"] == json.loads((sync_skill.LIBRARY_ROOT / "library.json").read_text())["version"]
