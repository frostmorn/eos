#!/usr/bin/python
import os
import shutil

# Path to directory containing boards
BOARD_PATH = "main/eboard"

board_list = []

def rm(file):
    if os.path.exists(file):
        os.remove(file)

def ensureroot():
    root_files = ["Makefile", "CMakeLists.txt", "main"]
    candidate_files = os.listdir()
    for rfile in root_files:
        if not(rfile in candidate_files):
            print("ERROR: Should be run from project root dir")
            exit(-1)

def loadboards():
    files = os.listdir(BOARD_PATH)
    for file in files:
        cur = BOARD_PATH+"/"+file
        if os.path.isdir(cur) and "sdkconfig" in os.listdir(cur):
            board_list.append(file)

def setboard(board):
    bpath = BOARD_PATH + "/" + board

    # Ensure openable
    if not(os.path.exists(bpath)):
        print("Unsupported board")
        exit(-1)

    print("Setting board " + board)

    # Rm old stuff. I wanted to use symlinks, but them ain't normally work 
    # with esp-idf.py build thingie. sad story. extemely sad
    rm("sdkconfig")
    rm("sdkconfig.old")

    shutil.copy(bpath+"/sdkconfig", ".") 

def selectboard():
    i = 1 # users prefer to count from 1
    for board in board_list:
        print(str(i) + ". " + board)
        i = i + 1

    selected = int(input("Enter your choice (1 - "+str(i-1)+"):").strip())
    setboard(board_list[selected-1])
    
ensureroot()
loadboards()
#setboard("lilka_v23")
selectboard()
