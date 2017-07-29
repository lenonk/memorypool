# Memory Pool
I started working on this by forking https://github.com/cacay/MemoryPool.  I needed a thread safe memory pool, and I wanted to avoid using locks if possible, so I forked his code and went to work.  

However, it quickly became aparant that I could not properly modify his design to be lock-free.  Odd bugs kept popping up, and it was clear to me that threads were stepping on each other.  So, I ripped everything out of his design, and started over.  

What's left is a project with similar function names, and a similar interface.  Everything else has been completely redesigned, but is still used in much the same way.  However, due to the lock-free nature of my code, it's significantly slower than his code.  

So, in conclusion, if you want a fast memory pool, and aren't interested in thread safety, grab a copy of his code.  If you want something that's thread safe, this should do the trick for you.

# Usage
Just include the header in your project.

Syntax should look something like this:

MemoryPool<YourObject, 1000> pool; // Where 1000 is the number of objects you want to pre-allocate

YourObject *yo = pool.new_element( <args> ); // Where args are passed to the constructor of YourObject
pool.delete_element(yo); // Returns the element to the pool, and calls the destructor

You can also use the allocate() and deallocate() members directly if you're not interested in calling constructors and destructors.
