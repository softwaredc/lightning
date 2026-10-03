from setuptools import setup, find_packages

setup(
    name="lightning-http",
    version="0.1.0",
    description="Python binding for lightning - extremely fast HTTP/1.1 parser",
    long_description="A ctypes-based wrapper over liblightning. Zero-copy, zero-allocation HTTP parsing with SIMD acceleration.",
    packages=find_packages(),
    python_requires=">=3.8",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: Apache Software License",
        "Topic :: Internet :: WWW/HTTP",
    ],
)
