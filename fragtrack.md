# Nordix/nfqueue-loadbalancer - Fragment tracking

We must track fragments, for example to store the computed (L4) hash
from the first fragment, also to store fragments out of order. The
fragment/connection tracker is implemented as a hash table;

<img src="fragtrack.svg" alt="Fragment track table" width="80%" />

