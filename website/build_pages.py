#!/usr/bin/python

import os
import re
import sys
def main():
    files = [file for file in os.listdir('pages') if file.endswith('.html')]
    include_pattern = re.compile('<!-- #include file="(.*)" -->')
    for file in files:
        sys.stdout.write('Building "%s"... ' % file)
        sys.stdout.flush()
        original_file = open(os.path.join('pages',file), 'r')
        new_file = open(file, 'w')
        for line in original_file.xreadlines():
            matches = include_pattern.findall(line)
            if matches:
                included_text = open(os.path.join('pages', 'includes', matches[0]),'r').read()
                sys.stdout.write('included "%s"... ' % matches[0])
                sys.stdout.flush()
                line = re.sub(include_pattern, included_text, line)
            new_file.write(line)
        original_file.close()
        new_file.close()
        print 'done.'
        

if __name__ == '__main__':
    main()
