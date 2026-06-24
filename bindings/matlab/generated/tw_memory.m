function [bytes] = tw_memory(self)
mex_id_ = 'c o double = tw_memory_w(c i treeweave_function*)';
[bytes] = treeweave_mex(mex_id_, self);

