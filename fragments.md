# Nordix/nfqueue-loadbalancer - Fragment handling

When a fragment arrive we hash on addresses only as described in the
[maglev document](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf).
If the lookup gives us our own fwmark we handle the fragment
locally. If not, route it to another load-balancer.


<img src="nfqueue-frag-routing.svg" alt="nfqueue frament routing" width="75%" />


Now we have made sure that all fragments from a particular source ends
up in the same load-balancer. Here we do the L4 hashing, including
ports, and store the hash value in a hash-table with key
`<src,dst,frag-id>`. Subsequent fragments will have the same `frag-id`
and we retrieve the stored hash value.

If the fragments are re-ordered and the first fragment, with the ports,
does not come first we have no option but it store fragments until the
first fragment arrives.

<img src="nfqueue-frag-reorder.svg" alt="nfqueue frament reorder" width="70%" />

1. The first fragment comes last

2. When we don't have a stored hash we copy the fragment in user-space
   and send `verdict=drop` so the kernel drops the original fragment.

3. When the first fragment arrives we compute and store the hash and
   load-balance the first fragment. We also initiate a re-inject of
   the stored fragments.

4. The stored fragments are injected to the kernel with a `tun`
   device. They are (again) redirected to user-space by the nfqueue
   but this time we have a stored hash and the fragments are
   load-balanced.


The NFQUEUE does not support stored packets to be re-injected, so some
other mechanism must be used for fragments, e.g. a raw socket or a tap
device.



## The unwanted re-assembly problem

If the Linux conntracker has *ever* been used in a netns (including
the main netns) packets are re-assembed by the kernel. Only whole
packets are received by the `nfqlb`. This is described in an excellent
way [here](https://unix.stackexchange.com/questions/650790/unwanted-defragmentation-of-forwarded-ipv4-packets).

One must be **very careful** to never used the Linux conntracker if
fragments should be handled.

This also means that if DNAT based load-balancing is used `nfqlb`
can't handle fragments.
