#!/usr/bin/env bash
# 构建 cadMousePro 并打成 Debian 包
#
# 用法:
#   ./build-deb.sh                 # 自动第3位+1；第3位到10则第2位+1、第3位归0
#   ./build-deb.sh 0.7.0           # 指定版本号（并写回模板）
#   ./build-deb.sh --skip-build    # 跳过 cmake，使用已有 src/build 产物
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${ROOT}/src"
TPL="${ROOT}/app-tpl/cn.oakdb.cadmouse"
BUILD_DIR="${SRC}/build"
OUT_DIR="${ROOT}/dist"
STAGE="${OUT_DIR}/pkg-root"
PKG_NAME="cn.oakdb.cadmouse"
APP_FILES="${STAGE}/opt/apps/${PKG_NAME}/files"

# 规范化为 x.y.z
normalize_semver() {
  local v="${1##*:}"
  v="${v%%-*}"
  if [[ "$v" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+) ]]; then
    echo "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}"
  elif [[ "$v" =~ ^([0-9]+)\.([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.0"
  elif [[ "$v" =~ ^([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]}.0.0"
  else
    echo ""
  fi
}

# 第3位+1；若第3位达到10，则第2位+1、第3位归0
bump_semver() {
  local v
  v="$(normalize_semver "$1")"
  if [[ -z "$v" ]]; then
    echo "无法解析版本号: $1" >&2
    return 1
  fi
  local major minor patch
  IFS=. read -r major minor patch <<<"$v"
  patch=$((patch + 1))
  if (( patch >= 10 )); then
    minor=$((minor + 1))
    patch=0
  fi
  echo "${major}.${minor}.${patch}"
}

write_version_files() {
  local ver="$1"
  local control_file="$2"
  local info_file="$3"

  local tmp
  tmp="$(mktemp)"
  awk -v ver="$ver" '
    BEGIN { done=0 }
    /^Version:/ { print "Version: " ver; done=1; next }
    { print }
    END { if (!done) print "Version: " ver }
  ' "$control_file" > "$tmp"
  mv "$tmp" "$control_file"

  if [[ -f "$info_file" ]]; then
    sed -i -E "s/\"version\"[[:space:]]*:[[:space:]]*\"[^\"]+\"/\"version\":\"${ver}\"/" "$info_file"
  fi
}

SKIP_BUILD=0
VERSION=""
VERSION_EXPLICIT=0

for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help)
      sed -n '2,10p' "$0"
      exit 0
      ;;
    *)
      if [[ "$arg" =~ ^[0-9] ]]; then
        VERSION="$arg"
        VERSION_EXPLICIT=1
      else
        echo "未知参数: $arg" >&2
        exit 2
      fi
      ;;
  esac
done

if [[ ! -d "$TPL" ]]; then
  echo "找不到包模板: $TPL" >&2
  exit 1
fi

TPL_CONTROL="${TPL}/DEBIAN/control"
TPL_INFO="${TPL}/opt/apps/${PKG_NAME}/info"

# 版本：未指定则从模板读取并自动递增，再写回模板
if [[ "$VERSION_EXPLICIT" -eq 1 ]]; then
  VERSION="$(normalize_semver "$VERSION")"
  if [[ -z "$VERSION" ]]; then
    echo "无效版本号参数" >&2
    exit 1
  fi
  echo "==> 使用指定版本: ${VERSION}"
else
  CUR_VER="$(sed -n 's/^Version:[[:space:]]*//p' "$TPL_CONTROL" | head -n1)"
  CUR_VER="$(normalize_semver "$CUR_VER")"
  if [[ -z "$CUR_VER" ]]; then
    echo "无法从模板读取版本号: $TPL_CONTROL" >&2
    exit 1
  fi
  VERSION="$(bump_semver "$CUR_VER")"
  echo "==> 版本递增: ${CUR_VER} -> ${VERSION}"
fi
write_version_files "$VERSION" "$TPL_CONTROL" "$TPL_INFO"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo "==> CMake 配置 & 编译 (Release)"
  cmake -S "$SRC" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j"$(nproc)"
else
  echo "==> 跳过编译，使用已有二进制"
fi

BIN="${BUILD_DIR}/cadmouse-gui"
HELPER="${BUILD_DIR}/cadmouse-helper"
CONF_SRC="${BUILD_DIR}/cadmouse.conf"
[[ -f "$CONF_SRC" ]] || CONF_SRC="${SRC}/cadmouse.conf"

if [[ ! -x "$BIN" ]]; then
  echo "找不到可执行文件: $BIN" >&2
  exit 1
fi
if [[ ! -x "$HELPER" ]]; then
  echo "找不到可执行文件: $HELPER" >&2
  exit 1
fi

echo "==> 准备打包目录"
rm -rf "$STAGE"
mkdir -p "$OUT_DIR"
cp -a "$TPL" "$STAGE"

# 注入构建产物
mkdir -p "${APP_FILES}"
install -m 0755 "$BIN" "${APP_FILES}/cadmouse-gui"
install -m 0755 "$HELPER" "${APP_FILES}/cadmouse-helper"
install -m 0644 "$CONF_SRC" "${APP_FILES}/cadmouse.conf"

# DEBIAN 脚本权限
chmod 0755 "${STAGE}/DEBIAN/postinst" "${STAGE}/DEBIAN/postrm"
chmod 0644 "${STAGE}/DEBIAN/control"

CONTROL="${STAGE}/DEBIAN/control"
INFO_JSON="${STAGE}/opt/apps/${PKG_NAME}/info"
INFO_VERSION="$VERSION"
write_version_files "$VERSION" "$CONTROL" "$INFO_JSON"

# 按主机架构更新 DEBIAN/control 与 deepin info
host_arch="$(dpkg --print-architecture 2>/dev/null || true)"
if [[ -z "$host_arch" ]]; then
  case "$(uname -m)" in
    x86_64|amd64) host_arch=amd64 ;;
    aarch64|arm64) host_arch=arm64 ;;
    i386|i686) host_arch=i386 ;;
    *) host_arch="$(uname -m)" ;;
  esac
fi
sed -i -E "s/^Architecture:.*/Architecture: ${host_arch}/" "$CONTROL"
arch="$host_arch"

if [[ -f "$INFO_JSON" ]]; then
  sed -i -E "s/\"arch\"[[:space:]]*:[[:space:]]*\[[^]]*\]/\"arch\":[\"${host_arch}\"]/" "$INFO_JSON"
  echo "==> info: version=${INFO_VERSION} arch=[\"${host_arch}\"]"
fi

# Depends：确保含 Concurrent（GUI 异步用）
if ! grep -q 'libqt5concurrent' "$CONTROL"; then
  sed -i 's/libqt5widgets5/libqt5widgets5, libqt5concurrent5/' "$CONTROL"
fi

# Installed-Size（单位 KiB）
size_kb="$(du -sk "$STAGE" | awk '{print $1}')"
sed -i -E "s/^Installed-Size:.*/Installed-Size: ${size_kb}/" "$CONTROL"

DEB_FILE="${OUT_DIR}/${PKG_NAME}_${VERSION}_${arch}.deb"

echo "==> 生成 ${DEB_FILE}"
# root-owner-group 避免打包进当前用户 uid
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$DEB_FILE"

echo "==> 完成"
dpkg-deb -I "$DEB_FILE"
echo
ls -lh "$DEB_FILE"
echo
echo "安装示例: sudo dpkg -i ${DEB_FILE}"
