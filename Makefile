.PHONY: help reqtool-install reqtool-test reqtool-lint reqtool-trace web-build web-preview

help:
	@echo "Available targets:"
	@echo "  reqtool-install   - install reqtool into local venv"
	@echo "  reqtool-test      - run reqtool unit tests"
	@echo "  reqtool-lint      - lint docs/requirements/"
	@echo "  reqtool-trace     - print traceability matrix"
	@echo "  web-build         - build web-config/dist/"
	@echo "  web-preview       - preview web-config locally"

reqtool-install:
	pip install -e tools/reqtool[dev]

reqtool-test:
	pytest tools/reqtool/tests -v

reqtool-lint:
	reqtool lint

reqtool-trace:
	reqtool trace

web-build:
	cd web-config && npm ci && npm run build

web-preview:
	cd web-config && npm run preview
