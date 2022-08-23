# Used by multibuild for building wheels
set -exo pipefail

CONFIG_DIR=$(abspath $(dirname "${BASH_SOURCE[0]}"))

ARCHIVE_SDIR=pyvmaf-depends
VMAF_VERSION=2.3.1
SCCACHE_VERSION=0.3.0
export PERLBREWURL=https://raw.githubusercontent.com/gugod/App-perlbrew/release-0.92/perlbrew

if grep -q cern /etc/yum.repos.d/devtools-2.repo 2>&1 2>/dev/null; then
    perl -pi -e 's#ftp\.riken\.jp/Linux#linuxsoft\.cern\.ch#g' /etc/yum.repos.d/devtools-2.repo
fi

function install_sccache {
    echo "::group::Install sccache"
    if [ -n "$IS_MACOS" ]; then
        brew install sccache
    elif [ ! -e /usr/local/bin/sccache ]; then
        local base_url="https://github.com/mozilla/sccache/releases/download/v$SCCACHE_VERSION"
        echo "base_url=$base_url"
        archive_name="sccache-v${SCCACHE_VERSION}-${PLAT}-unknown-linux-musl"
        echo "archive_name=$archive_name"
        echo "url=${base_url}/${archive_name}.tar.gz"
        echo "https://github.com/mozilla/sccache/releases/download/v0.3.0/sccache-v0.3.0-aarch64-unknown-linux-musl.tar.gz"
        fetch_unpack "${base_url}/${archive_name}.tar.gz"
        if [ -e "$archive_name/sccache" ]; then
            cp "$archive_name/sccache" "/usr/local/bin/sccache"
            chmod +x /usr/local/bin/sccache
        fi
    fi
    if [ -e /usr/local/bin/sccache ]; then
        export USE_SCCACHE=1
        export RUSTC_WRAPPER=/usr/local/bin/sccache
        export SCCACHE_DIR=$PWD/sccache
    fi
    echo "::endgroup::"
}

function install_meson {
    if [ -e meson-stamp ]; then return; fi

    install_ninja

    echo "::group::Install meson"
    if [ -n "$IS_MACOS" ]; then
        brew install meson
    else
        if [ "$MB_PYTHON_VERSION" == "2.7" ]; then
            local python39_exe=$(cpython_path 3.9)/bin/python
            $python39_exe -m pip install meson
            local meson_exe=$(dirname $python39_exe)/meson
            if [ "$(id -u)" != "0" ]; then
                sudo ln -s $meson_exe /usr/local/bin
            else
                ln -s $meson_exe /usr/local/bin
            fi
        else
            $PYTHON_EXE -m pip install meson
        fi
    fi
    echo "::endgroup::"

    touch meson-stamp
}

function install_ninja {
    if [ -e ninja-stamp ]; then return; fi
    echo "::group::Install ninja"
    if [ -n "$IS_MACOS" ]; then
        brew install ninja
    else
        $PYTHON_EXE -m pip install ninja
        local ninja_exe=$(dirname $PYTHON_EXE)/ninja
        ln -s $ninja_exe /usr/local/bin/ninja-build
    fi
    echo "::endgroup::"
    touch ninja-stamp
}

function build_vmaf {
    if [ -e vmaf-stamp ]; then return; fi

    install_meson
    install_ninja

    local cflags="$CFLAGS"
    local ldflags="$LDFLAGS"
    local meson_flags=()

    local CC="${CC:-gcc}"
    if [[ $(type -P sccache) ]]; then
        CC="sccache $CC"
    fi

    echo "::group::Build vmaf"
    fetch_unpack \
        "https://github.com/Netflix/vmaf/archive/v${VMAF_VERSION}.tar.gz" \
        "vmaf-$VMAF_VERSION.tar.gz"

    cat <<EOF > vmaf-$VMAF_VERSION/config.txt
[binaries]
c     = 'clang'
cpp   = 'clang++'
ar    = 'ar'
ld    = 'ld'
strip = 'strip'
[built-in options]
c_args = '$CFLAGS'
c_link_args = '$LDFLAGS'
[host_machine]
system = 'darwin'
cpu_family = 'aarch64'
cpu = 'arm'
endian = 'little'
EOF

    if [ "$PLAT" == "arm64" ]; then
        cflags=""
        ldflags=""
        meson_flags+=(--cross-file config.txt)
    fi

    (cd vmaf-$VMAF_VERSION \
        && CFLAGS="$cflags" LDFLAGS="$ldflags" CC="$CC" \
           meson setup libvmaf libvmaf/build \
              "--prefix=${BUILD_PREFIX}" \
              --default-library=static \
              --buildtype=release \
              -Denable_tests=false \
              -Denable_docs=false \
              -Dbuilt_in_models=true \
             "${meson_flags[@]}" \
        && SCCACHE_DIR="$SCCACHE_DIR" ninja -vC libvmaf/build install)
    if [ ! -n "$IS_MACOS" ]; then
      perl -pi -e 's/^(Libs: [^\n]+)$/$1 -lstdc++/' $BUILD_PREFIX/lib/pkgconfig/libvmaf.pc
    fi
    echo "::endgroup::"
    touch vmaf-stamp
}

function build_nasm {
    echo "::group::Build nasm"
    local CC="${CC:-gcc}"
    if [[ $(type -P sccache) ]]; then
        CC="sccache $CC"
    fi
    SCCACHE_DIR="$SCCACHE_DIR" CC="$CC" build_simple nasm 2.15.05 https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/
    echo "::endgroup::"
}

function ensure_sudo {
    if [ ! -e /usr/bin/sudo ]; then
        echo "::group::Install sudo"
        if [ -n "$IS_ALPINE" ]; then
            apk add sudo
        elif [[ $MB_ML_VER == "_2_24" ]]; then
            apt-get install -y sudo
        else
            yum_install sudo
        fi
        echo "::endgroup::"
    fi
}

function install_xxd {
    if [ ! -e /usr/bin/xxd ] || [ -n "$IS_ALPINE" ]; then
        echo "::group::Install xxd"
        fetch_unpack "http://grail.cba.csuohio.edu/~somos/xxd-1.10.tar.gz"
        (cd xxd-1.10 && make && chmod 755 xxd && cp xxd /usr/local/bin)
        echo "::endgroup::"
    fi
}

function append_licenses {
    echo "::group::Append licenses"
    for filename in $REPO_DIR/wheelbuild/dependency_licenses/*.txt; do
      echo -e "\n\n----\n\n$(basename $filename | cut -f 1 -d '.')\n" | cat >> $REPO_DIR/LICENSE
      cat $filename >> $REPO_DIR/LICENSE
    done
    echo -e "\n\n" | cat >> $REPO_DIR/LICENSE
    echo "::endgroup::"
}

function pre_build {
    echo "::endgroup::"

    append_licenses
    ensure_sudo
    install_sccache
    install_xxd

    local vmaf_build_dir="$REPO_DIR/depends/vmaf-$VMAF_VERSION/build"

    if [ ! -e "$vmaf_build_dir" ]; then
        if [ "$PLAT" != "arm64" ]; then
            build_nasm
        fi
        install_ninja
        install_meson
    fi

    build_vmaf

    echo "::group::Build wheel"
}

function run_tests {
    if ! $PYTHON_EXE -m unittest.mock 2>&1 2>/dev/null; then
        $PYTHON_EXE -m pip install mock
    fi
    # Runs tests on installed distribution from an empty directory
    (cd ../pyvmaf && pytest)
}

# Work around flakiness of pip install with python 2.7
if [ "$MB_PYTHON_VERSION" == "2.7" ]; then
    function pip_install {
        if [ "$1" == "retry" ]; then
            shift
            echo ""
            echo Retrying pip install $@
        else
            echo Running pip install $@
        fi
        echo ""
        $PIP_CMD install $(pip_opts) $@
    }

    function install_run {
        if [ -n "$TEST_DEPENDS" ]; then
            while read TEST_DEPENDENCY; do
                pip_install $TEST_DEPENDENCY \
                    || pip_install retry $TEST_DEPENDENCY \
                    || pip_install retry $TEST_DEPENDENCY \
                    || pip_install retry $TEST_DEPENDENCY
            done <<< "$TEST_DEPENDS"
            TEST_DEPENDS=""
        fi

        install_wheel
        mkdir tmp_for_test
        (cd tmp_for_test && run_tests)
        rmdir tmp_for_test  2>/dev/null || echo "Cannot remove tmp_for_test"
    }
fi
