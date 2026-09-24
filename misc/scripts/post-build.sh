#!/bin/bash

set -e

rm -rf ${OUT}
mkdir ${OUT}

mv ${NAME}.nso ${OUT}/${BINARY_NAME}
mv ${NAME}.npdm ${OUT}/main.npdm

if [ ! -z $ELF_EXTRACT ]; then
    cp "$NAME.elf" "$ELF_EXTRACT"
fi
