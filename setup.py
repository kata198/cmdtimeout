#!/usr/bin/env python

import os
import sys

from setuptools import setup

if __name__ == '__main__':

    summary = 'Execute an arbitrary command with a maximum given timeout'

    # Ensure we are in the same directory as this setup.py
    dirName = os.path.dirname(__file__)
    if dirName and os.getcwd() != dirName:
        os.chdir(dirName)
    try:
        with open('README.rst', 'rt') as f:
            long_description = f.read()
    except Exception as e:
        sys.stderr.write('Error reading description: %s\n' %(str(e),))
        long_description = summary

    setup(name='cmdtimeout',
        version='1.0.0',
        scripts=['cmdtimeout'],
        provides=['cmdtimeout'],
        keywords=['command', 'timeout', 'shell', 'execute'],
        url='https://github.com/kata198/cmdtimeout',
        long_description=long_description,
        author='Tim Savannah',
        author_email='kata198@gmail.com',
        maintainer='Tim Savannah',
        maintainer_email='kata198@gmail.com',
        license='GPLv3',
        description=summary,
        classifiers=['Development Status :: 5 - Production/Stable',
            'Programming Language :: Python',
            'License :: OSI Approved :: GNU General Public License v3 (GPLv3)',
            'Programming Language :: Python :: 2',
            'Programming Language :: Python :: 2.7',
            'Programming Language :: Python :: 3',
            'Programming Language :: Python :: 3.4',
            'Programming Language :: Python :: 3.5',
            'Programming Language :: Python :: 3.6',
            'Topic :: Utilities',
        ]
        
    )

#vim: set ts=4 sw=4 expandtab

