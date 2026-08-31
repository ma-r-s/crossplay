#!/bin/bash
# The pre-push hook, run against real pushes to a real (local) remote.
#
# Asserting on the hook's TEXT would prove nothing: the failure this guards is
# a push that succeeds when it should not, and only a push can show that. So
# every case here builds a bare remote, a clone with core.hooksPath set, and
# pushes for real.
#
# The four cases are the whole contract. The third is the one the hook exists
# for; the second and fourth are the ways a guard like this usually goes wrong
# -- refusing the owner, or refusing ordinary work.
#
#   host-tests/pushguard/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
HOOK="$ROOT/.githooks/pre-push"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL pushguard  $1"; }

[ -x "$HOOK" ] || { echo "FAIL pushguard  $HOOK missing or not executable"; exit 1; }

# A remote, and a clone that runs the hook. Committed with --no-verify so the
# repo's own pre-commit formatter is not dragged into this test.
setup() {
  rm -rf "$WORK/remote" "$WORK/clone"
  git init --quiet --bare "$WORK/remote"
  git init --quiet "$WORK/clone"
  (
    cd "$WORK/clone"
    git config user.email t@t; git config user.name t
    git config core.hooksPath "$WORK/hooks"
    git remote add origin "$WORK/remote"
    git checkout --quiet -b xteink
    echo base > file.txt
    git add -A && git commit --quiet --no-verify -m "base"
    git push --quiet origin xteink 2>/dev/null
  )
}
mkdir -p "$WORK/hooks"
cp "$HOOK" "$WORK/hooks/pre-push"
chmod +x "$WORK/hooks/pre-push"

commit_in_clone() {  # subject
  (
    cd "$WORK/clone"
    echo "$1" >> file.txt
    git add -A && git commit --quiet --no-verify -m "$1"
  )
}

# 1. An ordinary push, no bump anywhere: must go through untouched.
setup
commit_in_clone "fix: something ordinary"
if (cd "$WORK/clone" && git push --quiet origin xteink 2>/dev/null); then ok; else
  bad "an ordinary push was refused; the hook is crying wolf on every commit"
fi

# 2. The train owner: bump and tag pushed together, which is how a release
#    actually ships. Refusing this would break the thing it protects.
setup
commit_in_clone "chore: crossplay 9.9.9"
(cd "$WORK/clone" && git tag v9.9.9)
if (cd "$WORK/clone" && git push --quiet origin xteink v9.9.9 2>/dev/null); then ok; else
  bad "the owner's own 'push branch and tag together' was refused"
fi

# 3. THE BUG. Somebody else's bump, no tag anywhere: must be refused.
setup
commit_in_clone "chore: crossplay 9.9.9"
if (cd "$WORK/clone" && git push --quiet origin xteink 2>/dev/null); then
  bad "a bump with no tag was published; that is another session's release train"
else ok; fi

# 4. The override, for the deliberate case.
setup
commit_in_clone "chore: crossplay 9.9.9"
if (cd "$WORK/clone" && CROSSPLAY_ALLOW_UNTAGGED_BUMP=1 git push --quiet origin xteink 2>/dev/null); then ok; else
  bad "the documented override did not let the push through"
fi

# 5. The hook must be able to FAIL for the right reason, not merely exit
#    non-zero: check it names the version it refused.
setup
commit_in_clone "chore: crossplay 9.9.9"
msg="$( (cd "$WORK/clone" && git push origin xteink 2>&1 >/dev/null) )"
case "$msg" in
  *"crossplay 9.9.9"*) ok ;;
  *) bad "the refusal does not name the release it refused: $msg" ;;
esac

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
