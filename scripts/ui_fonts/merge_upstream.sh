#!/usr/bin/env bash
#
# Merges the upstream project into a derived project and rebuilds the UI font
# tables from the merged sources. See "Merging Into A Derived Project" in
# docs/cyd_ui_fonts.md.
#
#   scripts/ui_fonts/merge_upstream.sh [REF]       merge REF (default upstream/main)
#   scripts/ui_fonts/merge_upstream.sh --regen     only rebuild and stage the fonts,
#                                                  after resolving other conflicts
#
# The first time, the derived project does not have this script yet; run
# upstream's copy from the derived project's root:
#
#   git fetch upstream && git show upstream/main:scripts/ui_fonts/merge_upstream.sh | bash
#
# The generated tables differ between projects (each carries its own strings,
# and font_profile.json may add JIS X 0208), so they are always rebuilt after a
# merge - also when git merged them without a conflict, because then the
# upstream copy was taken as is. Nothing is committed: review, build, commit.
#
# English contract: run only in a project that has an `upstream` remote. In the
# upstream project itself (no such remote) the script does nothing.

set -euo pipefail

# The repository being merged into, not wherever this file lives: the first
# run pipes upstream's copy into bash.
REPO_ROOT="$(git rev-parse --show-toplevel)"
UI_FONTS="${REPO_ROOT}/scripts/ui_fonts"
GENERATED="components/support/cyd_ui_fonts/generated"
PROFILE="scripts/ui_fonts/font_profile.json"
PYTHON="${REPO_ROOT}/scripts/.venv/bin/python"

cd "${REPO_ROOT}"

regen() {
    if [[ ! -x "${PYTHON}" ]]; then
        echo "missing ${PYTHON}; set it up as in docs/cyd_ui_fonts.md (Regenerating)" >&2
        exit 1
    fi
    # A project that drew run-time text before the merge must still do so.
    if git cat-file -e "HEAD:${GENERATED}/message_font.json" 2>/dev/null &&
        ! grep -q '"body_jis_x0208"[[:space:]]*:[[:space:]]*true' "${PROFILE}"; then
        echo "${GENERATED}/message_font.json exists before the merge, but ${PROFILE}" >&2
        echo "does not set body_jis_x0208. Set it to true (or delete the JSON on purpose)" >&2
        echo "and run: scripts/ui_fonts/merge_upstream.sh --regen" >&2
        exit 1
    fi
    bash "${UI_FONTS}/fetch_fonts.sh"
    "${PYTHON}" "${UI_FONTS}/gen_ui_fonts.py"
    git add -A -- "${GENERATED}"
    echo
    echo "UI fonts rebuilt and staged (${PROFILE}: $(tr -d ' \n' < "${PROFILE}"))."
    echo "Build, check the result, then: git commit"
}

if [[ "${1:-}" == "--regen" ]]; then
    regen
    exit 0
fi

if ! git remote get-url upstream >/dev/null 2>&1; then
    echo "no 'upstream' remote: nothing to merge (this script is for derived projects)"
    exit 0
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "the working tree has uncommitted changes; commit or set them aside first" >&2
    exit 1
fi

REF="${1:-upstream/main}"
git fetch upstream

if git merge --no-commit --no-ff "${REF}"; then
    :
fi
if ! git rev-parse -q --verify MERGE_HEAD >/dev/null; then
    echo "nothing to merge: already up to date with ${REF}"
    exit 0
fi

# The tables are rebuilt below, so either side will do.
if git diff --name-only --diff-filter=U -- "${GENERATED}" | grep -q .; then
    git checkout --ours -- "${GENERATED}"
    git add -A -- "${GENERATED}"
fi

others="$(git diff --name-only --diff-filter=U)"
if [[ -n "${others}" ]]; then
    echo
    echo "Resolve these conflicts, then run: scripts/ui_fonts/merge_upstream.sh --regen" >&2
    echo "${others}" | sed 's/^/  /' >&2
    exit 1
fi

regen
