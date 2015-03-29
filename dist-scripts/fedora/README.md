How to compile, install and configure GMediaRender
--------------------------------------------------

Install and enable RPMFusion repository since we need 'ugly' plugins for gstreamer ::

    # yum -y install http://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-21.noarch.rpm   # Fedora 21
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
    $ rpmbuild -ba gmediarender-0.0.7/dist-scripts/fedora/gmediarender.spec

After the packages are built, they're be ready at ~/rpmbuild/RPMS/x86_64/ for installation. Source rpm will be ready at ~/rpmbuild/SRPMS/.

Installation can be done easily by ::

    # yum -y install gmediarender-0.0.7-1.fc21.x86_64.rpm

Note: I would avoid installing them by rpm -i or yum localinstall, as first alter db outside of yum, and latter is deprecated.

Additional configuration is also recommended, sice there's no configuration file for details, this can be achieved by modifing how systemd will start the service. If you want to modify the daemon to listen only on specific IP address and present itself with custom string, follow the steps ::

    # mkdir /etc/systemd/system/gmediarender.service.d
    # vi /etc/systemd/system/gmediarender.service.d/customize.conf   # or nano, or emacs, or whatever editor you like
    [Service]
    ExecStart=
    ExecStart=/usr/bin/gmediarender --port=49494 --ip-address=<your_IP_address> -f "DLNA Renderer GMediaRender"

    # systemctl daemon-reload
    # systemctl start gmediarender.service

If you are using FirewallD, you will also need to open firewall ports for GMediaRender. Custom service files are shipped with the package, just refresh FirewallD and add services to running configuration (and permanent as well, if desired) ::

    # firewall-cmd --reload
    # firewall-cmd --add-service=ssdp
    # firewall-cmd --add-service=gmediarender
