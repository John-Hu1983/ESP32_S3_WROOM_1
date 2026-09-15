# PyQt6 Command Guild

This project currently uses dynamic UI loading (main.py loads total.ui directly).
So compiling total.ui to total_ui.py is optional.


## 1) Run app directly
python.exe main.py

## 2) Compile UI to Python (optional)
python.exe -m PyQt6.uic.pyuic total.ui -o total_ui.py

## 3) Compile UI then run app
python.exe -m PyQt6.uic.pyuic total.ui -o total_ui.py ; python.exe main.py

## 4) Quick syntax check
python.exe -m py_compile main.py ble_manager.py ble_tab.py steering_tab.py user_config.py

## 5) Install required packages
python.exe -m pip install PyQt6 bleak

## 6) Upgrade pip (optional)
python.exe -m pip install --upgrade pip

## 7) Export dependencies (optional)
python.exe -m pip freeze > requirements.txt

## 8) Edit UI in Qt Designer (recommended)
Open total.ui in VS Code and use Designer view/edit flow.

Note:
- If you only edit total.ui and run main.py, UI changes still take effect.
- Generate total_ui.py only when you need Python source from UI.
