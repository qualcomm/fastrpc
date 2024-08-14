## Contributing to fastRPC User Mode Driver

Hi there!
We’re thrilled that you’d like to contribute to this project.
Your help is essential for keeping this project great and for making it better.

- [Before you begin](#before-you-begin)
- [Guidelines](#guidelines)
- [Branching strategy](#branching-strategy)
- [Setup](#setup)
- [Get code](#get-code)
- [Development](#development)
  * [Build](#build)
  * [Test](#test)
  * [Commit](#commit)
  * [Branch update](#branch-update)
  * [Branch cleanup](#branch-cleanup)
- [Submission](#submission)

## Before you begin
- Please read our [Code of Conduct](CODE-OF-CONDUCT.md) and [License](LICENSE) and ensure that you agree to abide by them.
- For every new feature or bug fix, always start a new issue on https://github.com/quic/fastrpc/issues.
- To contribute a bug-fix, please follow the steps in the next sections without any further discussion.
- To contribute new features, extensions, utility functions or other significant changes, please describe and discuss the change with us via the GitHub issue that you created above. **A pull request (PR) submitted without discussion and agreement with the project maintainers may be subject to rejection, or significant changes may be requested prior to its acceptance.**

## Guidelines
Please follow the guidelines below to increase the likelihood and speed of your PR acceptance:
- Follow the existing style in the file or folder where possible.
- Keep your change as focused as possible. If you want to make multiple independent changes, consider submitting them as separate PRs.
- Write a [good commit message](http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html).
- Every commit must be signed with the [Developer Certificate of Origin](https://developercertificate.org) (by adding the `-s` option to your `git commit` command).
- Each PR submission will trigger a build, test, code quality check and static analysis processes. Submitters are required to fix all failures and warnings prior to acceptance.

## Branching strategy
Contributors should develop on [their own fork](https://help.github.com/en/github/getting-started-with-github/fork-a-repo) on branches based off of the `development` branch and then pull requests should be made into the [upstream `development` branch](https://github.com/quic/fastrpc/tree/development).

## Setup
Go to https://github.com/quic/fastrpc and fork the repo using [these instructions](https://help.github.com/en/github/getting-started-with-github/fork-a-repo).

## Get code
[Sync your fork](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/syncing-a-fork) with the latest from the upstream repository.

Get the fastrpc code as follows:
```
git clone https://github.com/YOUR_USERNAME/fastrpc.git
cd fastrpc
```
*IMPORTANT:* Setup your pre-commit and commit-msg hook using the following way:
```
cd fastrpc
ln -s $(realpath -s .githooks/pre-commit) .git/hooks/pre-commit
ln -s $(realpath -s .githooks/commit-msg) .git/hooks/commit-msg
```

## Development
Start a new issue on https://github.com/quic/fastrpc/issues.

Create a branch for your feature
```
git checkout -b branch_short_feature_description
```

Now you may begin development. Once your development is complete, please ensure that the code builds successfully and that all tests pass using the instructions in the next sections.

### Build
Follow these steps to [build the code](https://github.com/quic/fastrpc/tree/development?tab=readme-ov-file#build--installation).

Verify that build artifacts got created here:
```
ls ./src/libs/
```

### Test
Run [unit tests](https://github.com/quic/fastrpc/tree/development?tab=readme-ov-file#testing).

### Commit
Commit the code and checkpoint it on your branch using the following procedure.

To display the files that you modified or added:
```
git status
```

To stage new (untracked) or existing files or folders for commit, do the following for each file or folder name that was added or changed:
```
git add <file or folder name that was added or changed>
```

To commit your changes:
```
git commit -s -m "Commit message"
```
>*IMPORTANT:* The -s option is required during the commit step (DCO signoff).

To push your branch to the remote:
```
git push origin branch_short_feature_description
```

### Branch update
Before merging, it is recommended that you update your branch to the latest on development using the following steps:
```
git fetch
git checkout development
git pull origin development
git checkout branch_short_feature_description
```
Rebase your changes:
```
git rebase development
```
Fix any conflicts that may arise. Then complete the rebasing procedure as follows:
```
git status

# Run the next 2 commands ONLY IF you needed to fix any conflicts.
# Run this for each file that you changed
git add <file or folder name that was added or changed>
git rebase --continue
```
Re-build the code on your branch and run all tests. Then update your remote branch:
```
git push origin branch_short_feature_description --force-with-lease
```

### Branch cleanup
It is recommended that you commit code to your branches often. Prior to pushing the code and submitting PRs, please try to clean up your branch by squashing multiple commits together and amending commit messages as appropriate. See these pages for details:  
https://blog.carbonfive.com/2017/08/28/always-squash-and-rebase-your-git-commits  
https://git-scm.com/book/en/v2/Git-Tools-Rewriting-History 

## Submission
When you're ready to submit your code, issue a pull request from the branch on your FORK into the develop branch on the upstream repository using these [instructions](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork).
1. Go to your forked repo page `https://github.com/YOUR_USERNAME/fastrpc` and click "New Pull Request".
1. Under "*compare changes*", select the base (destination) repository as `quic/fastrpc` and the branch as `base:development` to the left of the arrow.
1. Under "*compare changes*", select the head (source) repository as `YOUR_USERNAME/fastrpc` and the branch as `base:branch_short_feature_description` to the right of the arrow.
1. Click "*Create Pull Request*" which will initiate the PR and take you to the [PR page](https://github.com/quic/fastrpc/pulls).
    - In the PR page, click *Reviewers* on the top left and select one. He/she will receive an email notification.
    - In the PR page, click *Assignee* on the top left and select one. This person can be the reviewer or someone else or even the code submitter.
1. Wait for the outcome of the continuous integration (CI) build and test job, and for any review feedback from the project maintainers.