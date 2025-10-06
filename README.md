# sp25-cis5050-T10

Team members (full names):
- Sharon Yu
- Tianshi Miao
- Dominik Jau
- Franco Canova

SEAS logins:
- sharonyu
- miaots
- dominikk
- fcanova

Which features did you implement?
  (list features, or write 'Entire assignment')
- Entire assignment

Did you complete any extra-credit tasks (if any)? If so, which ones? (list extra-credit tasks, or write 'None')
- Yes
- Drive can efficiently (<5 s) handle uploads of large files (up to 150 MB, could be further increased if max tablet size was increased)

Did you personally write _all_ the code you are submitting (other than code from the course web page)?
- [x] Yes
- [ ] No

Did you copy any code from the Internet, or from classmates?
- [ ] Yes
- [x] No

Did you collaborate with anyone on this assignment?
- [x] Yes (Team members)
- [ ] No

Did you use an AI tool such as ChatGPT for this assignment?
- [ ] Yes
- [x] No

## Instructions for compiling and deploying the project
The following commands runs the entire project with two replication groups, each of three servers:
- If you are using a mac, please run ./makeProject_mac.sh
- If you are using linux, please run ./makeProject.sh
- Admin path: 127.0.0.1:8080/admin
- Load balancer path: 127.0.0.1:8880
- Rerunning a KVStorage node: ./kvstorage -p \<port> -c \<server-config> [-i \<initialization-directory>] (for initial state) [-w \<working-directory>] [-v] (verbose) [-r] (recover flag)

**IMPORTANT:** Please make sure that in any two subsequent runs of the solution, you delete the files inside cache_2 before rerunning the project.
