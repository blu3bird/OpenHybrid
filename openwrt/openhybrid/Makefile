include $(TOPDIR)/rules.mk
 
PKG_NAME:=openhybrid
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
 
PKG_BUILD_DIR:=$(BUILD_DIR)/openhybrid-$(PKG_VERSION)

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/blu3bird/OpenHybrid.git
PKG_SOURCE_VERSION:=b2ca8d135a69c3e1c204814727ee485d6ac80847
PKG_SOURCE_DATE:=2019-02-10

PKG_MIRROR_HASH:=0fcd49089e864b35e9bec42bdc6cb03b95078ad79b5fb12964158d9d3d35e8d3

PKG_BUILD_DEPENDS:=libmnl
PKG_BUILD_DEPENDS+=busybox

include $(INCLUDE_DIR)/package.mk

MAKE_PATH:=src
 
PKG_ARCH:=all

define Package/openhybrid
	DEPENDS:=+libmnl
	TITLE:=OpenHybrid - An RFC8157 implementation.
	SECTION:=base
	CATEGORY:=Network
	URL:=https://github.com/blu3bird/OpenHybrid
endef
 
define Package/openhybrid/description
	An open client implementation of RFC8157.
	In other words: A way to use MagentaZuhause Hybrid without a Speedport Hybrid router.
endef
 
define Package/openhybrid/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/openhybrid $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc/openhybrid
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/examples/openhybrid.conf $(1)/etc/openhybrid
endef
 
$(eval $(call BuildPackage,openhybrid,libmnl))
