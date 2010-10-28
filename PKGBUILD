# Maintainer: Onni R. <onnir at iki dot fi>
pkgname=yuxtapa
pkgver=2
pkgrel=1
license=('None')
pkgdesc="a text-mode team-based real-time multiplayer game"
arch=('i686' 'x86_64')
url="http://tempoanon.net/lotuskip/yuxtapa"
makedepends=('boost')
depends=('zlib' 'boost-libs' 'ncurses')
source=(http://tempoanon.net/lotuskip/tervat/$pkgname-$pkgver.tar.gz)
install=yuxtapa.install
md5sums=('487f92649485748562b76cb29fe51131')

build() {
  mkdir -p "${pkgdir}/usr/bin" || return 1
  mkdir -p "${pkgdir}/usr/share/yuxtapa" || return 1
  mkdir -p "${pkgdir}/usr/share/doc" || return 1
  cd $srcdir/$pkgname
  aclocal || return 1
  automake --add-missing || return 1
  autoconf || return 1
  ./configure || return 1
  make || return 1
}

package() {
  cd $srcdir/$pkgname
  mv -f yuxtapa_sv yuxtapa_cl ${pkgdir}/usr/bin
  mv -f tmplates/*.conf ${pkgdir}/usr/share/yuxtapa
  mv -f manual ${pkgdir}/usr/share/doc/yuxtapa
}
