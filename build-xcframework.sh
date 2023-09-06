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
# realm-monorepo-xcframework-v12.4.0-7-g0b90cacec.tar.xz => realm-core-12.4.0-xcframework.tar.xz
SEMVER=$(git describe | cut -d'-' -f1 | cut -c2-)
mv "realm-monorepo-xcframework-$VERSION.tar.xz" "realm-core-$SEMVER-xcframework.tar.xz"

open .
