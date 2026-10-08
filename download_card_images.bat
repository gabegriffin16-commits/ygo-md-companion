@echo off
cd /d "%~dp0"
py download_card_images.py || python download_card_images.py
pause
