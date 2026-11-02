# Shared by ci/release-local.sh and .github/workflows/release.yml.

# program_changed <tag> <commit>: true when anything that ends up in a
# package differs between the two. Everything counts except files that
# only describe or build packages for the release pipeline, and docs.
program_changed() {
    ! git diff --quiet "$1" "$2" -- . \
        ':!packaging/fedora' ':!packaging/debian' ':!packaging/stage.sh' \
        ':!ci' ':!.github' ':!docs' ':!release' ':!README.md' ':!.gitignore'
}
