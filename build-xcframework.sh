#!/bin/zsh
set -e

VERSION=$(git describe)

echo "Build XCframework (librealm-monorepo)"
echo "DEVELOPER_DIR=$DEVELOPER_DIR"
echo "VERSION=$VERSION"

echo "Do you want to continue? (y/n)"
read -r answer
case $answer in
    [yY][eE][sS]|[yY])
        echo "building..."
        ;;
    *)
        exit 0
        ;;
esac

./tools/build-cocoa.sh -b
open .
