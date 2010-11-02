# Maintainer: Onni R. <onnir at iki dot fi>
pkgname=yuxtapa
pkgver=3
pkgrel=1
license=('None')
pkgdesc="a text-mode team-based real-time multiplayer game"
arch=('i686' 'x86_64')
url="http://tempoanon.net/lotuskip/yuxtapa"
makedepends=('boost')
depends=('zlib' 'boost-libs' 'ncurses')
source=(http://tempoanon.net/lotuskip/tervat/$pkgname-$pkgver.tar.gz)
install=yuxtapa.install
md5sums=('3a1bdac6acbedc8adcff883bbad01f35')

build() {
  mkdir -p "${pkgdir}/usr/bin" || return 1
  mkdir -p "${pkgdir}/usr/share/yuxtapa" || return 1
  mkdir -p "${pkgdir}/usr/share/doc" || return 1
  cd $srcdir/$pkgname
  make || return 1
}

package() {
  cd $srcdir/$pkgname
  mv -f yuxtapa_sv yuxtapa_cl ${pkgdir}/usr/bin
  mv -f tmplates/*.conf ${pkgdir}/usr/share/yuxtapa
  mv -f manual ${pkgdir}/usr/share/doc/yuxtapa
}
