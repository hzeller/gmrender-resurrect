How to compile, install and configure GMediaRender from GitHub sources
----------------------------------------------------------------------

Install and enable Nux's repository since we need 'ugly' plugins for gstreamer ::
    # yum -y install http://li.nux.ro/download/nux/dextop/el7/x86_64/nux-dextop-release-0-1.el7.nux.noarch.rpm
    # yum -y update

Install pre-requisite packages ::
    # yum -y install gstreamer1 gstreamer1-devel gstreamer1-plugins-ugly gstreamer1-plugins-bad-free gstreamer1-plugins-base gstreamer1-plugins-good libupnp-devel

Install build tools ::
    # yum -y install autoconf automake gcc rpmdevtools git

(Optionally, create less priviledged user for building the rpms' as you should not use root) ::
    # useradd makerpm
    # passwd makerpm
    # su - makerpm

Build the packages ::
    $ rpmdev-setuptree
    $ cd rpmbuild/SOURCES
    $ git clone https://github.com/martinstefany/gmrender-resurrect.git
    $ mv gmrender-resurrect gmediarender-0.0.7
    $ tar cjvf gmediarender-0.0.7.tar.bz2 gmediarender-0.0.7
    $ rpmbuild -ba gmediarender-0.0.7/dist-scripts/centos7/gmediarender.spec

After the packages are built, they're be ready at ~/rpmbuild/RPMS/x86_64/ for installation. Source rpm will be ready at ~/rpmbuild/SRPMS/.

Installation can be done easily by ::

    # yum -y install /rpmbuild/RPMS/x86_64/gmediarender-0.0.7-1.el7.centos.x86_64.rpm

Note: I would avoid installing them by rpm -i or yum localinstall, as first alter db outside of yum, and latter is deprecated.

Additional configuration is also recommended, sice there's no configuration file for details, this can be achieved by modifing how systemd will start the service. If you want to modify the daemon to listen only on specific IP address and present itself with custom string, follow the steps ::

    # mkdir /etc/systemd/system/gmediarender.d
    # vi /etc/systemd/system/gmediarender.d/customize.conf   # or nano, or emacs, or whatever editor you like
    [Service]
    ExecStart=/usr/bin/gmediarender --port=49494 --ip-address=<your_IP_address> -f "DLNA Renderer GMediaRender"

    # systemctl daemon-reload
    # systemctl start gmediarender.service

And you're done.
