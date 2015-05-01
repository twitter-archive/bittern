NAME=$(shell rpm -q --qf '%{name}\n' --specfile *.spec | head -1)
VERSION=$(shell rpm -q --qf '%{version}\n' --specfile *.spec | head -1)
sources:
	git archive --prefix=$(NAME)-$(VERSION)/ -o $(NAME)-$(VERSION).tar.gz HEAD
