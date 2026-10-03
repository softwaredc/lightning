#!/usr/bin/env bash
set -e
cd /mnt/d/TigerCo/知识分享业务群/github/lightning/deps
for tag in v9.2.1 release/v9.2.1 v9.1.3 release/v9.1.3 main; do
    code=$(curl -s -o /dev/null -w '%{http_code}' --connect-timeout 10 \
        "https://raw.githubusercontent.com/nodejs/llhttp/$tag/src/llhttp.h")
    echo "tag=$tag -> $code"
    if [ "$code" = "200" ]; then
        echo "FOUND: $tag"
        curl -fsSL --connect-timeout 15 -o llhttp.h "https://raw.githubusercontent.com/nodejs/llhttp/$tag/src/llhttp.h"
        curl -fsSL --connect-timeout 15 -o llhttp.c "https://raw.githubusercontent.com/nodejs/llhttp/$tag/src/llhttp.c"
        curl -fsSL --connect-timeout 15 -o api.c "https://raw.githubusercontent.com/nodejs/llhttp/$tag/src/api.c"
        curl -fsSL --connect-timeout 15 -o helpers.c "https://raw.githubusercontent.com/nodejs/llhttp/$tag/src/helpers.c"
        ls -la llhttp* api.c helpers.c
        exit 0
    fi
done
echo "no tag worked"
exit 1
