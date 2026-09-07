# #!/bin/bash

# WEB_HASH=`${1}/apps/webget cs144.keithw.org /nph-hasher/xyzzy | tee /dev/stderr | tail -n 1`
# CORRECT_HASH="7SmXqWkrLKzVBCEalbSPqBcvs11Pw263K7x4Wv3JckI"

# if [ "${WEB_HASH}" != "${CORRECT_HASH}" ]; then
#     echo ERROR: webget returned output that did not match the test\'s expectations
#     exit 1
# fi
# exit 0


#!/bin/bash

WEB_OUTPUT=`${1}/apps/webget www.msftconnecttest.com /connecttest.txt 2>&1`
BODY=`echo "${WEB_OUTPUT}" | tail -n 1`
EXPECTED="Microsoft Connect Test"

if echo "${WEB_OUTPUT}" | grep -q "HTTP/1.1 200 OK" && [ "${BODY}" = "${EXPECTED}" ]; then
    echo "Check passed: www.msftconnecttest.com returned Success"
    exit 0
else
    echo "ERROR: webget returned output that did not match expectations"
    echo "Actual output:"
    echo "${WEB_OUTPUT}"
    exit 1
fi