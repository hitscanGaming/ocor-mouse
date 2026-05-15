# reqtool

Requirements & traceability CLI for the ocor-mouse monorepo.

## Install

    pip install -e tools/reqtool[dev]

## Commands

    reqtool lint                                     # validate frontmatter
    reqtool trace                                    # markdown matrix to stdout
    reqtool trace --diff --base origin/main          # PR-comment-ready diff
    reqtool orphans                                  # implementation gaps
    reqtool checklist --tag v0.1.0                   # QA issue body
    reqtool report --tag v0.1.0 --output trace.md    # release report

## Tests

    pytest tools/reqtool/tests -v
