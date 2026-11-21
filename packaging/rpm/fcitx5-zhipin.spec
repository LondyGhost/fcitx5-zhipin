Name:           fcitx5-zhipin
Version:        1.1.0
Release:        1%{?dist}
Summary:        Intelligent ABC style input method engine for fcitx5 (librime backend)

License:        GPL-3.0-or-later
URL:            https://github.com/LondyGhost/fcitx5-zhipin
# Local build: point Source0 at the generated tarball, e.g.
#   cd .. && tar czf fcitx5-zhipin-1.0.0.tar.gz --transform 's,^,fcitx5-zhipin-1.0.0/,' .
Source0:        fcitx5-zhipin-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  fcitx5-devel
BuildRequires:  librime-devel
BuildRequires:  rime-data
Requires:       fcitx5
Requires:       librime
Requires:       rime-data
Recommends:     rime-ice

%description
An fcitx5 input method engine that reproduces the classic Windows
"Intelligent ABC" typing flow on top of an embedded librime session:
type pinyin silently, press space to reveal candidates, space again to
commit the highlighted choice, Enter/Shift+letter for raw letters, and
-/= for paging. Chinese punctuation and staged per-syllable character
picking are supported.

%prep
%setup -q -n fcitx5-zhipin-%{version}

%build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_DOCDIR=%{_datadir}/doc/fcitx5-zhipin
cmake --build build

%install
DESTDIR=%{buildroot} cmake --install build

%files
%license LICENSE
%doc README.md
# fcitx5 addon 库在多数发行版装 /usr/lib/fcitx5（fcitx5 上游路径方案），
# 不用 libdir 宏（lib64 发行版会指错）。
/usr/lib/fcitx5/libabc.so
%{_datadir}/fcitx5/addon/abc.conf
%{_datadir}/fcitx5/inputmethod/abc.conf
%{_datadir}/rime-data/abc.schema.yaml
%{_datadir}/fcitx5/themes/abc/theme.conf
%{_datadir}/icons/hicolor/scalable/apps/fcitx5-zhipin.svg

%changelog
* Sat Nov 21 2026 abc-im maintainers <dev@example.com> - 1.1.0-1
- New: syllable separator (') and user phrase memory (user_phrases.tsv).
- New: clickable candidates; Shift-tap mode switch detection.
- Fix: English mode swallowed letters; punctuation handling in composition;
  -/= paging vs preedit; syllable re-segmentation on code change.
- Fix: non-blocking rime deployment; warn when fcitx5-rime is also enabled.
* Mon Nov 16 2026 abc-im maintainers <dev@example.com> - 1.0.1-1
- Fix dictionary lookup: prefix-extension candidates follow exact matches.
- Fix preeditInClient fallback to documented default (false).
- Deploy tests: isolate system-root checks via ABC_TEST_SYSTEM_ROOT.
- build-abc.sh: reject invalid --staging before configure (protect build cache).
* Mon Nov 02 2026 abc-im maintainers <dev@example.com> - 1.0.0-1
- Initial release.
