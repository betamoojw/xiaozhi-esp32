git checkout feat/knx_ip

source $IDF_PATH/export.sh

python .\scripts\build.py --list-boards

Build: 
python .\scripts\build.py lckfb/szpi-esp32s3 --name lckfb-lichuang-dev
python .\scripts\build.py lckfb/szpi-esp32s3 --name szpi-esp32s3
python .\scripts\build.py lckfb/szpi-esp32s3 (*verified)

idf.py fullclean



The current project's CI uses exactly this style of invocation:
source $IDF_PATH/export.sh
python .\scripts\build.py <board> --name <variant>

Release as ZIP:
python .\scripts\build.py lckfb/szpi-esp32s3 --name lckfb-lichuang-dev --zip