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

# rename to standard name
mv "realm-monorepo-xcframework-$VERSION.tar.xz" "realm-core-xcframework.tar.xz"

echo ""
echo "Notes:"
echo "Remember to upload the tar to https://github.com/gongzhang/realm-core/releases"
echo "The corresponding files in realm-swift repo are 'dependencies.list' and 'scripts/download-core.sh'"
open .
