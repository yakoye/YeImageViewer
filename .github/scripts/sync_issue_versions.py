"""用公开的 GitHub Releases 更新问题反馈表单的版本下拉列表。"""

import json
from pathlib import Path
import re
import sys


VERSION = re.compile(r"v?([0-9]+)\.([0-9]+)\.([0-9]+)(-[0-9A-Za-z.-]+)?")
START = "        # BEGIN RELEASE OPTIONS\n"
END = "        # END RELEASE OPTIONS\n"
FORM = Path(__file__).resolve().parents[1] / "ISSUE_TEMPLATE" / "bug_report.yml"


def release_options(releases):
    stable, previews = [], []
    for release in releases:
        if release["draft"] or not release["published_at"]:
            continue
        tag = release["tag_name"]
        match = VERSION.fullmatch(tag)
        if not match or bool(match[4]) != release["prerelease"]:
            continue
        major, minor, patch = (int(part) for part in match.groups()[:3])
        group = previews if release["prerelease"] else stable
        group.append(((major, minor, patch), release["published_at"], tag))

    # 稳定版按发布时间取最近三项；已转正的候选版不再占用一项。
    tags = [entry[2] for entry in sorted(stable, key=lambda entry: entry[1:], reverse=True)[:3]]
    stable_version = max((entry[0] for entry in stable), default=(-1, -1, -1))
    if previews and (newest := max(previews))[0] > stable_version:
        tags.append(newest[2])
    return tags


def main():
    pages = json.load(sys.stdin)
    options = release_options(release for page in pages for release in page)
    text = FORM.read_text(encoding="utf-8")
    if text.count(START) != 1 or text.count(END) != 1:
        raise ValueError("问题反馈模板缺少唯一的版本选项标记")
    start = text.index(START) + len(START)
    end = text.index(END, start)
    updated = text[:start] + "".join(f"        - {tag}\n" for tag in options) + text[end:]
    if updated != text:
        FORM.write_text(updated, encoding="utf-8", newline="\n")
    print("版本选项：" + ("、".join(options) or "仅显示手动选项"))


if __name__ == "__main__":
    main()
