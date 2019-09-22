#!/usr/bin/python
# ./genseq.py xxxxxxxxxx 2
#  2 lines of xxxxxxxxxx random chars 
import random
import sys,string
len=len(sys.argv[1])
for i in range(int(sys.argv[2])):
  print string.join(map( lambda x: chr(random.randrange(ord('a'),ord('z'),1)) , range(len) ),'')
