Name:           gmediarender
Version:        0.0.7
Release:        1%{?dist}
Summary:        Resource efficient UPnP/DLNA renderer

License:        LGPLv2+
URL:            https://github.com/hzeller/gmrender-resurrect
Source0:        https://github.com/hzeller/gmrender-resurrect/%{name}-%{version}.tar.bz2

BuildRequires:  gstreamer1
BuildRequires:  gstreamer1-devel
BuildRequires:  gstreamer1-plugins-ugly
BuildRequires:  gstreamer1-plugins-bad-free
BuildRequires:  gstreamer1-plugins-base
BuildRequires:  gstreamer1-plugins-good
BuildRequires:  libupnp-devel

Requires:  gstreamer1
Requires:  gstreamer1-plugins-ugly
Requires:  gstreamer1-plugins-bad-free
Requires:  gstreamer1-plugins-base
Requires:  gstreamer1-plugins-good
Requires:  libupnp
Requires(pre): shadow-utils


%description
GMediaRender is a resource efficient UPnP/DLNA renderer.

%setup -q -n %{name}-%{version}
./autogen.sh

%build
%configure 
make

%pre
getent group gmediarender >/dev/null || groupadd -r gmediarender
getent passwd gmediarender >/dev/null || \
    useradd -r -g gmediarender -G audio -M -d /usr/share/gmediarender -s /sbin/nologin \
    -c "GMediaRender DLNA/UPnP Renderer" gmediarender
exit 0

%install
mkdir -p $RPM_BUILD_ROOT/%{_bindir}
cp ./src/gmediarender $RPM_BUILD_ROOT/%{_bindir}

mkdir -p $RPM_BUILD_ROOT/%{_unitdir}
cp ./dist-scripts/centos7/%{name}.service $RPM_BUILD_ROOT/%{_unitdir}

mkdir -p $RPM_BUILD_ROOT/usr/share/gmediarender
cp ./data/grender-64x64.png $RPM_BUILD_ROOT/usr/share/gmediarender
cp ./data/grender-128x128.png $RPM_BUILD_ROOT/usr/share/gmediarender

%post
if [ $1 -eq 1 ] ; then
    /bin/systemctl enable %{name}.service >/dev/null 2>&1 || :
    /bin/systemctl start %{name}.service >/dev/null 2>&1 || :
fi
  
%preun
if [ $1 -eq 0 ] ; then
    # Package removal, not upgrade
    /bin/systemctl --no-reload disable %{name}.service > /dev/null 2>&1 || :
    /bin/systemctl stop %{name}.service > /dev/null 2>&1 || :
fi
  
%postun
getent passwd gmediarender >/dev/null && userdel gmediarender
getent group gmediarender >/dev/null && groupdel gmediarender
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ] ; then
    # Package upgrade, not uninstall
    /bin/systemctl try-restart %{name}.service >/dev/null 2>&1 || :
fi
exit 0

%files
%attr(0755,root,root) %{_bindir}/gmediarender
%config(noreplace) %{_unitdir}/%{name}.service
%attr(0755,gmediarender,gmediarender) /usr/share/gmediarender/
%attr(0644,gmediarender,gmediarender) /usr/share/gmediarender/grender-64x64.png
%attr(0644,gmediarender,gmediarender) /usr/share/gmediarender/grender-128x128.png

%changelog
* Mon Dec 01 2014 <admin@vortexbox.org>
- Updated for CentOS7, added automatic system user/group add and removal upon installation
* Mon Sep 16 2013 <admin@vortexbox.org>
- Initial release
