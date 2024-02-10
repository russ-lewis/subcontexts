# Description
Initializes and manages subcontexts within a process.

# Usage
Initialize a subcontext with
```
scon_t scon_create(const char *libpath);
```
where libpath is the path to a shared library (\*.so).

To call a function in the subcontext use
```
void *scon_callf(scon_t scon, const char *funcname, void *arg)
```
a function in a subcontext must have the signature `void *func(void *arg)`.
This means that if you need a function that takes multiple arguments, you must wrap it in a function 
with this signature and pass in a pointer to a struct with the arguments for the inner function.

Close the subcontext with
```
void scon_close(scon_t scon);
```
