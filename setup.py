import ast
from io import open
import os
import sys

from setuptools import Extension, setup


def version():
    filename = 'src/pyvmaf/__init__.py'
    with open(filename) as f:
        tree = ast.parse(f.read(), filename)
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target, = node.targets
            if isinstance(target, ast.Name) and target.id == '__version__':
                return node.value.s


def readme():
    try:
        with open('README.md') as f:
            return f.read()
    except IOError:
        pass


IS_DEBUG = hasattr(sys, "gettotalrefcount")
PLATFORM_MINGW = os.name == "nt" and "GCC" in sys.version

setup(
    name='pyvmaf',
    description='A python extension for libvmaf',
    long_description=readme(),
    long_description_content_type="text/markdown",
    version=version(),
    ext_modules=[
        Extension(
            'pyvmaf._vmaf',
            ["src/pyvmaf/_vmaf.c"],
            depends=["libvmaf/libvmaf.h"],
            libraries=["vmaf"]),
    ],
    package_data={'': ['README.rst']},
    package_dir={"": "src"},
    packages=['pyvmaf'],
    license='MIT License',
    author='Frankie Dintino',
    author_email='fdintino@theatlantic.com',
    url='https://github.com/fdintino/pyvmaf/',
    download_url='https://github.com/fdintino/pyvmaf/releases',
    install_requires=[],
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Environment :: Web Environment',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Programming Language :: C',
        'Programming Language :: C++',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: Implementation :: CPython',
        'Programming Language :: Python :: Implementation :: PyPy',
        "Topic :: Multimedia :: Graphics",
        "Topic :: Multimedia :: Graphics :: Graphics Conversion",
    ],
    zip_safe=not(IS_DEBUG or PLATFORM_MINGW))
