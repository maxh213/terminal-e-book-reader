# Maintainer: Your Name <your@email.com>
pkgname=tread
pkgver=0.1.0
pkgrel=1
pkgdesc="Terminal EPUB reader with themes, TOC, link following, and reading position persistence"
arch=('x86_64')
url="https://github.com/yourusername/tread"
license=('MIT')
depends=('libzip' 'libxml2' 'ncurses')
makedepends=('gcc' 'make' 'pkg-config')
source=("$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$pkgname-$pkgver"
    make
}

package() {
    cd "$pkgname-$pkgver"
    make DESTDIR="$pkgdir" PREFIX=/usr install
}
