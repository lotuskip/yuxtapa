# Maintainer: Onni R. <onnir at iki dot fi>
# (Not really maintained anymore -- since I've stopped active use of Arch -- but
# shipped for people to fiddle with.)
#
# See comment below on how to install an IPv6 bot (by default this installs v4).
pkgname=yuxtapa
pkgver=9a
pkgrel=1
license=('None')
pkgdesc="a text-mode team-based real-time multiplayer game"
arch=('i686' 'x86_64')
url="http://github.com/lotuskip/yuxtapa"
depends=('zlib' 'ncurses')
source=(http://github.com/downloads/lotuskip/yuxtapa/$pkgname-$pkgver.tar.gz)
install=yuxtapa.install
md5sums=('d5a76e747bc8c9d489c3b003e9b86823')

build() {
  cd $srcdir/$pkgname
# uncomment next line for IPv6 bots:
# sed -i 's/EXTRA_FLAGS=/EXTRA_FLAGS=-DBOT_IPV6/' Makefile
  make
  make mrbrown
}

package() {
  mkdir -p "${pkgdir}/usr/bin"
  mkdir -p "${pkgdir}/usr/share/yuxtapa"
  mkdir -p "${pkgdir}/usr/share/doc"
  cd $srcdir/$pkgname
  mv -f yuxtapa_sv yuxtapa_cl ${pkgdir}/usr/bin
  mv -f tmplates/*.conf mrbrown ${pkgdir}/usr/share/yuxtapa
  mv -f tmplates/gen_map_tmplate.sh ${pkgdir}/usr/share/yuxtapa
  chmod +x ${pkgdir}/usr/share/yuxtapa/gen_map_tmplate.sh
  mv -f tmplates/vim ${pkgdir}/usr/share/yuxtapa
  mv -f manual ${pkgdir}/usr/share/doc/yuxtapa
}
