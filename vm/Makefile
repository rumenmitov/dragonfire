REPO    := $(realpath $(CURDIR)/..)
VM_DIR  := $(CURDIR)/.vm
SSH_KEY := $(firstword $(wildcard ~/.ssh/id_ed25519 ~/.ssh/id_rsa))

SSH := ssh -p 2222 -i $(SSH_KEY) \
	-o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null \
	root@localhost

IMG_URL := https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
IMG     := $(VM_DIR)/noble-server-cloudimg-amd64.img


.PHONY: setup
setup:
	mkdir -p $(VM_DIR)/seed

	curl -L -o $(IMG) -C - $(IMG_URL)

	qemu-img create -f qcow2 -b $(IMG) -F qcow2 $(VM_DIR)/disk.qcow2 10G

	sed "s|SSH_AUTHORIZED_KEY_PLACEHOLDER|$$(cat $(SSH_KEY).pub)|" \
	  $(CURDIR)/cloud-config-template.yaml > $(VM_DIR)/seed/user-data

	cp $(CURDIR)/meta-data.yaml $(VM_DIR)/seed/meta-data

	cloud-localds $(VM_DIR)/seed.iso \
	  $(VM_DIR)/seed/user-data $(VM_DIR)/seed/meta-data


.PHONY: start
start:
	qemu-system-x86_64 \
	  -accel kvm -cpu host \
	  -m 2G -smp 2 \
	  -display none -serial null \
	  -daemonize \
	  -pidfile $(VM_DIR)/qemu.pid \
	  -drive file=$(VM_DIR)/disk.qcow2,if=virtio \
	  -drive file=$(VM_DIR)/seed.iso,if=virtio,format=raw \
	  -net nic \
	  -net user,hostfwd=tcp::2222-:22 \
	  -virtfs local,path=$(REPO),mount_tag=host,security_model=mapped-xattr

	@echo "VM started (pid $$(cat $(VM_DIR)/qemu.pid)). Waiting for SSH..."
	@until $(SSH) true 2>/dev/null; do sleep 1; done
	@echo "VM ready. Use 'make ssh' to connect."


.PHONY: stop
stop:
	$(SSH) poweroff 2>/dev/null || true
	@while [ -f $(VM_DIR)/qemu.pid ] && \
	  kill -0 $$(cat $(VM_DIR)/qemu.pid) 2>/dev/null; \
	  do sleep 1; done
	@rm -f $(VM_DIR)/qemu.pid
	@echo "VM stopped."


.PHONY: ssh
ssh:
	$(SSH)


.PHONY: provision
provision:
	$(SSH) "apt-get update && \
	  apt-get install -y build-essential linux-headers-\$$(uname -r) rsync"

	$(SSH) "modprobe 9p && modprobe 9pnet_virtio"

	$(SSH) "mkdir -p /mnt/host"

	$(SSH) "grep -q 'host /mnt/host' /etc/fstab || \
	  echo 'host /mnt/host 9p trans=virtio,rw 0 0' >> /etc/fstab"

	$(SSH) "mount /mnt/host 2>/dev/null || true"


RSYNC_SSH := ssh -p 2222 -i $(SSH_KEY) \
	-o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null

.PHONY: sync-headers
sync-headers:
	mkdir -p $(REPO)/.kheaders
	rsync -az --delete \
	  -e "$(RSYNC_SSH)" \
	  root@localhost:/lib/modules/$$($(SSH) uname -r)/build/include/ \
	  $(REPO)/.kheaders/include/
	rsync -az --delete \
	  -e "$(RSYNC_SSH)" \
	  root@localhost:/lib/modules/$$($(SSH) uname -r)/build/arch/ \
	  $(REPO)/.kheaders/arch/


.PHONY: build
build:
	$(SSH) "cd /mnt/host && make"

.PHONY: load
load:
	$(SSH) "insmod /mnt/host/build/$(name).ko $(args)"

.PHONY: unload
unload:
	$(SSH) "rmmod $(name) || true"

.PHONY: dmesg
dmesg:
	$(SSH) "dmesg | tail -20"
