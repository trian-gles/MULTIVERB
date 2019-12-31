struct Foo { float f_member };

struct Foo gFoo;	// global: This test is not testing structs declared inside a function

struct Foo returnAFoo() {
	gFoo.f_member = 11;
	return gFoo;
}

x = returnAFoo();

if (x.f_member != 11) {
	printf("ERROR: struct returned via function call did not preserve member value")
	exit(1)
}
