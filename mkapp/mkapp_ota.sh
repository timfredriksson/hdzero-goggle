#!/bin/bash
set -x

function make_img_md5() {
    ${BIN_DIR}/md5sum $1 | awk '{print $1}' > $1.md5
}

function get_app_version() {
	local delimeter=' = '
    local file=$1
	local section="app"
    local key="ver"
    local val=$(awk -F "$delimeter" '/\['${section}'\]/{a=1}a==1&&$1~/'${key}'/{print $2;exit}' $file)
    echo ${val}
}

ROOT_DIR=$PWD

BIN_DIR=$ROOT_DIR/bin
OTA_DIR=$ROOT_DIR/ota_app
IMG_DIR=$ROOT_DIR/image
APP_DIR=$ROOT_DIR/app

APP_SIZE=12582912
APP_IMAGE=${OTA_DIR}/app.fex
APP_VERSION=$(get_app_version "$ROOT_DIR/app/version")

rm -rf $OTA_DIR
mkdir -p $OTA_DIR

${BIN_DIR}/mkfs.jffs2 -l -e 0x10000 \
	-p ${APP_SIZE} \
	-d ${APP_DIR} \
	-o ${APP_IMAGE}
make_img_md5 ${APP_IMAGE}

cp $IMG_DIR/system-*.img $OTA_DIR
${BIN_DIR}/sunxi-mbr patch $OTA_DIR/system-*.img app ${APP_IMAGE}
make_img_md5 $OTA_DIR/system-*.img

pushd $OTA_DIR
tar cvf $OTA_DIR/hdzgoggle_app_ota-${APP_VERSION}.tar *
popd