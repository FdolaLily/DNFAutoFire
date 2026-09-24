"""Read project-only Huorong evidence without changing protection or trust settings.

Copies SQLite databases and their WAL files into a private temporary directory,
queries the copies in read-only mode, and deletes those copies before returning.
An empty event list is NOT proof that a scan ran or that an artifact is clean.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import datetime as dt
import hashlib
import json
import pathlib
import shutil
import sqlite3
import tempfile


DATABASES = ("log.db", "wlfile.db", "user.db")
RELATED_TABLES = {
    "wlfile.db": {
        "TrustRegion_60": ("fn", "hash"),
        "WhiteListPath_60": ("value",),
        "WhiteListHash_60": ("value",),
    },
    "user.db": {
        "HipsKModException_60": ("value",),
        "ProcGrant_60": ("fn",),
        "RiskSoftware_60": ("procname", "paths"),
        "AppRunCtrl_60": ("fn",),
    },
}


def signature(path: pathlib.Path):
    try:
        s = path.stat()
        return s.st_size, s.st_mtime_ns
    except FileNotFoundError:
        return None


def snapshot(source: pathlib.Path, target: pathlib.Path, name: str) -> None:
    # Retry a moving source; never checkpoint or open the live database for write.
    files = (name, name + "-wal", name + "-shm")
    for _ in range(3):
        before = {f: signature(source / f) for f in files}
        if before[name] is None:
            raise FileNotFoundError(source / name)
        for f in files:
            (target / f).unlink(missing_ok=True)
            if before[f] is not None:
                shutil.copyfile(source / f, target / f)
        if before == {f: signature(source / f) for f in files}:
            return
    raise RuntimeError(f"{name} kept changing while taking a read-only snapshot; retry later")


@contextmanager
def connection(path: pathlib.Path):
    con = sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)
    try:
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA query_only=ON")
        yield con
    finally:
        con.close()


def related_rows(con, table, columns, paths, hashes):
    if not con.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)
    ).fetchone():
        return {"available": False, "rows": []}
    conditions, params = [], []
    for col in columns:
        # Identifiers come exclusively from RELATED_TABLES above.
        conditions.extend((f"lower({col}) LIKE ?", f"lower({col}) LIKE ?"))
        params.extend(("%dnfautofire%", "%autohotkey%"))
        value = f"lower(replace(replace({col},char(92)||char(92),char(92)),'*','%'))"
        for path in paths:
            # Match exact files, wildcard rules, and ancestor directory rules.
            conditions.extend((f"lower(?) LIKE {value}", f"lower(?) LIKE rtrim({value},char(92))||char(92)||'%'"))
            params.extend((path, path))
        for h in hashes:
            conditions.append(f"lower({col})=?")
            params.append(h)
    rows = con.execute(f"SELECT * FROM {table} WHERE " + " OR ".join(conditions), params)
    return {"available": True, "rows": [dict(row) for row in rows]}


def collect(source, output, artifacts, since_id):
    root = pathlib.Path(__file__).resolve().parent.parent
    paths = [
        pathlib.Path(r"E:\autokill\DNFAutoFire.exe"),
        root / "dist" / "DNFAutoFire.exe",
        pathlib.Path(r"D:\workspace\AutoManagerProcess\DNFAutoFire.exe"),
        root / "venv/tools/autohotkey-v1.1.37.02/Compiler/Ahk2Exe.exe",
        root / "venv/tools/autohotkey-v1.1.37.02/AutoHotkeyU64.exe",
        *artifacts,
    ]
    paths = list(dict.fromkeys(str(p.resolve()) for p in paths))
    fingerprints, hashes = [], set()
    for name in paths:
        path = pathlib.Path(name)
        row = {"path": name, "exists": path.is_file()}
        if row["exists"]:
            content = path.read_bytes()
            for algorithm in ("md5", "sha1", "sha256"):
                digest = hashlib.new(algorithm, content).hexdigest()
                row[algorithm] = digest
                hashes.add(digest)
        fingerprints.append(row)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = pathlib.Path(tempfile.mkdtemp(prefix=".huorong-readonly-", dir=output.parent)).resolve()
    report = {
        "capturedAt": dt.datetime.now().astimezone().isoformat(),
        "sourceDirectory": str(source),
        "sinceEventId": since_id,
        "limitation": "Read-only log/trust evidence only. No scan was requested; absence of new detections does not mean a scan passed.",
        "artifacts": fingerprints,
        "relatedTrustAndExceptionRows": {},
    }
    try:
        for name in DATABASES:
            snapshot(source, temp, name)
        with connection(temp / "log.db") as con:
            report["latestEventId"] = con.execute("SELECT max(id) FROM HrLogV3_60").fetchone()[0]
            conditions = ["lower(detail) LIKE '%dnfautofire%'"]
            params = [since_id]
            for h in sorted(hashes):
                conditions.append("lower(detail) LIKE ?")
                params.append("%" + h + "%")
            rows = con.execute(
                "SELECT * FROM HrLogV3_60 WHERE id>? AND (" + " OR ".join(conditions) + ") ORDER BY id",
                params,
            )
            events = []
            for row in rows:
                event = dict(row)
                event["time"] = dt.datetime.fromtimestamp(event["ts"], dt.timezone.utc).astimezone().isoformat()
                try:
                    event["detail"] = json.loads(event["detail"])
                except (ValueError, TypeError):
                    pass
                events.append(event)
            report["projectEvents"] = events
            report["logCategoryCounts"] = [dict(row) for row in con.execute(
                "SELECT fid,fname,count(*) AS count FROM HrLogV3_60 GROUP BY fid,fname"
            )]
        for name, tables in RELATED_TABLES.items():
            with connection(temp / name) as con:
                report["relatedTrustAndExceptionRows"][name] = {
                    table: related_rows(con, table, columns, paths, sorted(hashes))
                    for table, columns in tables.items()
                }
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    finally:
        # Only exact files in this invocation's freshly created directory.
        for name in DATABASES:
            for suffix in ("", "-wal", "-shm", "-journal"):
                (temp / (name + suffix)).unlink(missing_ok=True)
        temp.rmdir()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=pathlib.Path, default=pathlib.Path(r"C:\ProgramData\Huorong\Sysdiag"))
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--artifact", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--since-id", type=int, default=0)
    args = parser.parse_args()
    report = collect(args.source_dir.resolve(), args.output.resolve(), args.artifact, args.since_id)
    matches = sum(len(t["rows"]) for db in report["relatedTrustAndExceptionRows"].values() for t in db.values())
    print(json.dumps({"report": str(args.output.resolve()), "latestEventId": report["latestEventId"], "projectEvents": len(report["projectEvents"]), "relatedTrustOrExceptionRows": matches}, ensure_ascii=False))


if __name__ == "__main__":
    main()
