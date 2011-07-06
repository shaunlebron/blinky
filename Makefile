
all: theory.js

theory.js: theory.coffee
	coffee -c theory.coffee
