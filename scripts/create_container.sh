#!/bin/bash

usage() {
    echo "Usage: $0 [OPTIONS] <workspace-dir>"
    echo ""
    echo "Options:"
    echo "  -n, --name <name>   Container name (default: c2cuda-dev)"
    echo "  -h, --help          Show this help message and exit"
    echo ""
    echo "Arguments:"
    echo "  workspace-dir   Host directory to mount as /workspace in the container"
    echo ""
    echo "Examples:"
    echo "  $0 /home/hlt/project/Auto-C-To-CUDA"
    echo "  $0 --name my-container /asb/path"
    echo "  $0 --name my-container ."
    exit 0
}

CONTAINER_NAME="c2cuda-dev"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            ;;
        -n|--name)
            if [[ -z "$2" || "$2" == -* ]]; then
                echo "Error: '--name' requires a non-empty argument."
                exit 1
            fi
            CONTAINER_NAME="$2"
            shift 2
            ;;
        -*)
            echo "Error: Unknown option '$1'"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
        *)
            WORKSPACE_DIR="$1"
            shift
            ;;
    esac
done

if [[ -z "$WORKSPACE_DIR" ]]; then
    echo "Error: workspace directory is required."
    echo "Run '$0 --help' for usage."
    exit 1
fi

WORKSPACE_DIR="$(realpath "$WORKSPACE_DIR")"

if [[ ! -d "$WORKSPACE_DIR" ]]; then
    echo "Error: Directory '$WORKSPACE_DIR' does not exist."
    exit 1
fi

docker run -it \
    --gpus all \
    --security-opt label=disable \
    -v "${WORKSPACE_DIR}:/workspace" \
    --name "$CONTAINER_NAME" \
    c2cuda-dev
