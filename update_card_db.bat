@echo off
cd /d "%~dp0"
py update_card_db.py || python update_card_db.py
pause
