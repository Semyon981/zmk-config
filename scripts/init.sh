python -m venv .venv
source .venv/bin/activate

pip install west
west init -l config
west update
west zephyr-export
pip install -r zephyr/scripts/requirements-base.txt
pip install "cmake<4.4"
west sdk install -t arm-zephyr-eabi