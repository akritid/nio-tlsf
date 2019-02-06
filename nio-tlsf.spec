Name:          nio-tlsf
Version:       0.4
Release:       <CI_CNT>.<B_CNT>%{?dist}
License:       BSD
Source:        %{name}-%{version}.tar.gz
Group:         Libraries/Development
Summary:       Two-Level Segregated Fit memory allocator library

%description
Two-Level Segregated Fit memory allocator implementation,
modified for Niometrics.

%package devel
Summary:  Two-Level Segregated Fit memory allocator library development files
Group:    Libraries/Development
Requires: nio-tlsf = %{version}-%{release}

%description devel
This package includes header files for the nio-tlsf library.

%prep
%setup -q

%build

%configure
%__make

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%clean
%__rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{_libdir}/libnio-tlsf.so*

%files devel
%{_includedir}/*
%{_libdir}/libnio-tlsf.la
%{_libdir}/libnio-tlsf.a
%{_libdir}/pkgconfig/nio-tlsf.pc

%changelog

* Tue Jun 26 2018 Periklis Akritidis <akritid@niometrics.com>  [0.1-1]
- Initial packaging.
