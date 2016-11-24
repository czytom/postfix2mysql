Name: mysqmail
Version: __VERSION__
Release: 0.1.20090818
License: LGPL
Group: System Environment/Daemons
URL: http://www.gplhost.com/software-mysqmail.html
Source: mysqmail-%{version}.tar.gz
BuildRoot:%{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: make gcc dotconf-devel mysql-devel

Requires: dotconf /usr/bin/tail mysql
Summary: Use MySQL accouting and auth for most used MTA (configuration file)
Group: System Environment/Daemons
%description
MySQMail is a set of tiny daemon loggers for Qmail, Postfix,
Pure-ftpd and Courier that will save trafic informations in database.
It's also a replacement for the qmail standard checkpasswd that
does the auth via a MySQL table. When done, it setups 2 more
environment variables: MYSQMAIL_USERNAME MYSQMAIL_DOMAINNAME
that the mysqmail's qmail-pop3d replacement will use to do
the traffic accounting in the MySQL table for this account.
This package holds the configuration file management for
the other packages which share the same /etc/mysqmail.conf:
mysqmail-postfix-logger and mysqmail-courier-logger

%package postfix-logger
Summary: Use MySQL accouting and auth for most used MTA (postfix logger)
Group: System Environment/Daemons
Requires: dtc-core, mysqmail
%description postfix-logger
MySQMail is a set of tiny daemon loggers for Qmail, Postfix,
Pure-ftpd and Courier that will save trafic informations in database.
It's also a replacement for the qmail standard checkpasswd that
does the auth via a MySQL table. When done, it setups 2 more
environment variables: MYSQMAIL_USERNAME MYSQMAIL_DOMAINNAME
that the mysqmail's qmail-pop3d replacement will use to do
the traffic accounting in the MySQL table for this account.
This package holds the configuration file management for
the other packages which share the same /etc/mysqmail.conf:
mysqmail-postfix-logger and mysqmail-courier-logger

%package courier-logger
Summary: Use MySQL accouting and auth for most used MTA (courier logger)
Group: System Environment/Daemons
Requires: dtc-core, mysqmail
%description courier-logger
MySQMail is a set of tiny daemon loggers for Qmail, Postfix,
Pure-ftpd and Courier that will save trafic informations in database.
It's also a replacement for the qmail standard checkpasswd that
does the auth via a MySQL table. When done, it setups 2 more
environment variables: MYSQMAIL_USERNAME MYSQMAIL_DOMAINNAME
that the mysqmail's qmail-pop3d replacement will use to do
the traffic accounting in the MySQL table for this account.
This package holds the configuration file management for
the other packages which share the same /etc/mysqmail.conf:
mysqmail-postfix-logger and mysqmail-courier-logger

%package pure-ftpd-logger
Summary: Use MySQL accouting and auth for most used MTA (pure-ftpd logger)
Group: System Environment/Daemons
Requires: dtc-core, mysqmail
%description pure-ftpd-logger
MySQMail is a set of tiny daemon loggers for Qmail, Postfix,
Pure-ftpd and Courier that will save trafic informations in database.
It's also a replacement for the qmail standard checkpasswd that
does the auth via a MySQL table. When done, it setups 2 more
environment variables: MYSQMAIL_USERNAME MYSQMAIL_DOMAINNAME
that the mysqmail's qmail-pop3d replacement will use to do
the traffic accounting in the MySQL table for this account.
This package holds the configuration file management for
the other packages which share the same /etc/mysqmail.conf:
mysqmail-postfix-logger and mysqmail-courier-logger

%package dovecot-logger
Summary: Use MySQL accouting and auth for most used MTA (dovecot logger)
Group: System Environment/Daemons
Requires: dtc-core, mysqmail
%description dovecot-logger
MySQMail is a set of tiny daemon loggers for Qmail, Postfix,
Pure-ftpd and Courier that will save trafic informations in database.
It's also a replacement for the qmail standard checkpasswd that
does the auth via a MySQL table. When done, it setups 2 more
environment variables: MYSQMAIL_USERNAME MYSQMAIL_DOMAINNAME
that the mysqmail's qmail-pop3d replacement will use to do
the traffic accounting in the MySQL table for this account.
This package holds the configuration file management for
the other packages which share the same /etc/mysqmail.conf:
mysqmail-postfix-logger and mysqmail-courier-logger

%prep
%setup

%build
make

%install

set -e

%{__rm} -rf %{buildroot}
mkdir -p %{buildroot}
make install DESTDIR=%{buildroot} SBIN_DIR=%{_sbindir} MAN_DIR=%{_mandir} INSTALL=install
make install-conf DESTDIR=%{buildroot} ETCDIR=%{_sysconfdir} INSTALL=install
install -D -m 0755 etc/init.d/mysqmail-postfix-logger %{buildroot}%{_sysconfdir}/rc.d/init.d/mysqmail-postfix-logger
install -D -m 0755 etc/init.d/mysqmail-courier-logger %{buildroot}%{_sysconfdir}/rc.d/init.d/mysqmail-courier-logger
install -D -m 0755 etc/init.d/mysqmail-pure-ftpd-logger %{buildroot}%{_sysconfdir}/rc.d/init.d/mysqmail-pure-ftpd-logger
install -D -m 0755 etc/init.d/mysqmail-dovecot-logger %{buildroot}%{_sysconfdir}/rc.d/init.d/mysqmail-dovecot-logger

%pre

%clean
%{__rm} -rf %{buildroot} 2>&1 >/dev/null

%files
%defattr(-, root, root, -)
%config(noreplace) %{_sysconfdir}/mysqmail.conf
%{_mandir}/man?/*

%files postfix-logger
%{_sbindir}/mysqmail-postfix-logger
%{_sysconfdir}/rc.d/init.d/mysqmail-postfix-logger
#%{_mandir}/man8/mysqmail-postfix-logger.8
%files courier-logger
%{_sbindir}/mysqmail-courier-logger
%{_sysconfdir}/rc.d/init.d/mysqmail-courier-logger
%files dovecot-logger
%{_sbindir}/mysqmail-dovecot-logger
%{_sysconfdir}/rc.d/init.d/mysqmail-dovecot-logger
%files pure-ftpd-logger
%{_sbindir}/mysqmail-pure-ftpd-logger
%{_sysconfdir}/rc.d/init.d/mysqmail-pure-ftpd-logger

%changelog
* Sat Aug 08 2009 Thomas Goirand (zigo) <thomas@goirand.fr> 0.30.4-0.1.20090818
- Initial Package
