# ETIL Project Build Modifications
Dated 20260316

# Observed Problems:

The VM experiment pointed out some big failings in the build system:

- Too many rebuilds.
- Not using static/shared libraries in the (local) builds to shorten builds.
- Not using git branches.
- Bumping the version at branch creation: Requires changes to Super Push.


## Too Many Rebuilds:

### When we execute a plan we:
- Modify code/config/docs
- Run cmake sometimes 
- Recompile everything, both debug and release
- Test everything, both debug and release.
- Loop back to midifying code if there are errors. 

The problem is should only be building and testing the debug version first, and when 
it compiles and tests successfully *THEN* build the release and test it. 
That will stop excessive recompiles while the code is modified to make the plans work.

## Problem: Not using static/shared libraries in the (local) builds to shorten builds:

- The CI build system relies on using pre-build libraries. 

#### _REMEMBER_: ***Pre-built libraries for dependencies shall be the standard for ETIL builds from now on.***

- The ${WORKSPACE}/lib (hereafter the 'lib' directory) directory will be where the libaries will be located locally.
- On the CI system their currently located at ```/opt/etil-deps/v1/{release|debug}```. 
- The lib directory path needs to have an environment override in any scripting.
- The cmake file should unify the build such that the pre-build libraries are built if their not existant.
- The cmake file should not constantly rebuild the libraries if the etil source code has changed.
- Build the libs in 'build' and 'build-debug' directories under the lib directory.
- CI build containers needs access to the libraries via volume (preferred) or bind mounts.

## Problem: Not using git branches:

- We should be working from git branches, not on master in preparation for working on GitHub.
- Branches will have version bumps as their first commit. See below...
i.e, we should be bumping when taking the branch. 
  - Multiple contributors will have to do an additional version bump at merge time.

## Enhancement: Bumping the version at branch creation:

- When the development of a new feature or bug fix is started, a new branch will be created.
- Find the current ETIL version in the cmake file and calculate the bump'ed version: Keep that for use below.
- The new branch name shall be 'YYYYMMDDThhmmss-vX.Y.Z' where X.Y.Z is the new bumped version number.
- When the new branch has been created the ETIL version in the cmake file shall be bumped and committed, but not pushed.

- Super Push will now bump the ETIL version *only* if multiple conflicting branches have already bumped the version and their versions will conflict.
- In the case of a version conflict Super Push shall take the highest of the existing version numbers within the new branch(s) and master 
and increment that version in trhe cmake file and commit that version as part of the Super Push commit/push.
- If there is only one branch or there is no version conflicts then Super Push shall use the existing version number in the Super Push commit and push without modification.
- When the situation is ambiguous Super Push shall prompt for guidance.


