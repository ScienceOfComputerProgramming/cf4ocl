/*   
 * This file is part of cf4ocl (C Framework for OpenCL).
 * 
 * cf4ocl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as 
 * published by the Free Software Foundation, either version 3 of the 
 * License, or (at your option) any later version.
 * 
 * cf4ocl is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public 
 * License along with cf4ocl. If not, see 
 * <http://www.gnu.org/licenses/>.
 * */
 
/** 
 * @file
 * Functions for obtaining information about OpenCL entities
 * such as platforms, devices, contexts, queues, kernels, etc.
 * 
 * @author Nuno Fachada
 * @date 2014
 * @copyright [GNU Lesser General Public License version 3 (LGPLv3)](http://www.gnu.org/licenses/lgpl.html)
 * */
 
#include "abstract_wrapper.h"

/* Table of all existing wrappers. */
static GHashTable* wrappers = NULL;
/* Define lock for synchronizing access to table of all existing 
 * wrappers. */
G_LOCK_DEFINE(wrappers);

/**
 * Create a new ::CCLWrapper object. This function is called by 
 * the concrete wrapper constructors and should not be called by client
 * code.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] cl_object OpenCL object to wrap.
 * @param[in] size Size in bytes of wrapper.
 * @return A new wrapper object.
 * */
CCLWrapper* ccl_wrapper_new(void* cl_object, size_t size) {

	/* Make sure OpenCL object is not NULL. */
	g_return_val_if_fail(cl_object != NULL, NULL);

	/* The new wrapper object. */
	CCLWrapper* w;
	
	/* Lock access to table of all existing wrappers. */
	G_LOCK(wrappers);
	
	/* If table of all existing wrappers is not yet initialized,
	 * initialize it. */
	if (wrappers == NULL) {
		wrappers = g_hash_table_new_full(
			g_direct_hash, g_direct_equal, NULL, NULL);
	}
	
	/* Check if requested wrapper already exists, and get it if so. */
	w = g_hash_table_lookup(wrappers, cl_object);
	if (w == NULL) {
		/* Wrapper doesn't yet exist, create it. */
		w = (CCLWrapper*) g_slice_alloc0(size);
		w->cl_object = cl_object;
		/* Insert newly created wrapper in table of all existing
		 * wrappers. */
		g_hash_table_insert(wrappers, cl_object, w);
	}
	
	/* Increase reference count of wrapper. */
	ccl_wrapper_ref(w);
	
	/* Unlock access to table of all existing wrappers. */
	G_UNLOCK(wrappers);
	
	/* Return requested wrapper. */
	return w;
}

/** 
 * Increase the reference count of the wrapper object.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper The wrapper object. 
 * */
void ccl_wrapper_ref(CCLWrapper* wrapper) {
	
	/* Make sure wrapper object is not NULL. */
	g_return_if_fail(wrapper != NULL);
	
	/* Increment wrapper reference count. */
	g_atomic_int_inc(&wrapper->ref_count);
	
}

/** 
 * Decrements the reference count of the wrapper object.
 * If it reaches 0, the wrapper object is destroyed.
 * 
 * @protected @memberof ccl_wrapper
 *
 * @param[in] wrapper The wrapper object.
 * @param[in] size Size in bytes of wrapper object.
 * @param[in] rel_fields_fun Function for releasing specific wrapper 
 * fields.
 * @param[in] rel_cl_fun Function for releasing OpenCL object.
 * @param[out] err Return location for a GError, or NULL if error 
 * reporting is to be ignored. The only error which may be reported by 
 * this function is if some problem occurred when releasing the OpenCL 
 * object.
 * @return CL_TRUE if wrapper was destroyed (i.e. its ref. count reached
 * zero), CL_FALSE otherwise.
 * */
cl_bool ccl_wrapper_unref(CCLWrapper* wrapper, size_t size,
	ccl_wrapper_release_fields rel_fields_fun,
	ccl_wrapper_release_cl_object rel_cl_fun, GError** err) {
	
	/* Make sure wrapper object is not NULL. */
	g_return_val_if_fail(wrapper != NULL, CL_FALSE);
	
	/* Make sure err is NULL or it is not set. */
	g_return_val_if_fail(err == NULL || *err == NULL, CL_FALSE);

	/* Flag which indicates if wrapper was destroyed or not. */
	cl_bool destroyed = CL_FALSE;
	
	/* OpenCL status flag. */
	cl_int ocl_status;
	
	/* Decrement reference count and check if it reaches 0. */
	if (g_atomic_int_dec_and_test(&wrapper->ref_count)) {

		/* Ref. count reached 0, so wrapper will be destroyed. */
		destroyed = CL_TRUE;
		
		/* Release the OpenCL wrapped object. */
		if (rel_cl_fun != NULL) {
			ocl_status = rel_cl_fun(wrapper->cl_object);
			if (ocl_status != CL_SUCCESS) {
				g_set_error(err, CCL_ERROR, CCL_ERROR_OCL,
				"%s: unable to create release OpenCL object (OpenCL error %d: %s).", 
				G_STRLOC, ocl_status, ccl_err(ocl_status));
			}
		}
		
		/* Destroy hash table containing information. */
		if (wrapper->info != NULL) {
			g_hash_table_destroy(wrapper->info);
		}
		
		/* Remove wrapper from static table, release static table if
		 * empty. */
		G_LOCK(wrappers);
		g_hash_table_remove(wrappers, wrapper->cl_object);
		if (g_hash_table_size(wrappers) == 0) {
			g_hash_table_destroy(wrappers);
			wrappers = NULL;
		}
		G_UNLOCK(wrappers);
		
		/* Destroy remaining wrapper fields. */
		if (rel_fields_fun != NULL)
			rel_fields_fun(wrapper);
		
		/* Destroy wrapper. */
		g_slice_free1(size, wrapper);

	}
	
	/* Return flag indicating if wrapper was destroyed. */
	return destroyed;

}

/**
 * Returns the wrapper object reference count. For debugging and 
 * testing purposes only.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper The wrapper object.
 * @return The wrapper object reference count or -1 if wrapper is NULL.
 * */
int ccl_wrapper_ref_count(CCLWrapper* wrapper) {
	
	/* Make sure wrapper is not NULL. */
	g_return_val_if_fail(wrapper != NULL, -1);
	
	/* Return reference count. */
	return wrapper->ref_count;

}

/**
 * Get the wrapped OpenCL object.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper The wrapper object.
 * @return The wrapped OpenCL object.
 * */
void* ccl_wrapper_unwrap(CCLWrapper* wrapper) {

	/* Make sure wrapper is not NULL. */
	g_return_val_if_fail(wrapper != NULL, NULL);
	
	/* Return the OpenCL wrapped object. */
	return wrapper->cl_object;
}

/**
 * Add a ::CCLWrapperInfo object to the info table of the
 * given wrapper.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper Wrapper to add info to.
 * @param[in] param_name Name of parameter which will refer to this 
 * info.
 * @param[in] info Info object to add.
 * */
void ccl_wrapper_add_info(CCLWrapper* wrapper, cl_uint param_name,
	CCLWrapperInfo* info) {
		
	/* Make sure wrapper is not NULL. */
	g_return_if_fail(wrapper != NULL);
	/* Make sure info is not NULL. */
	g_return_if_fail(info != NULL);

	/* If information table is not yet initialized, then
	 * initialize it. */
	if (wrapper->info == NULL) {
		wrapper->info = g_hash_table_new_full(
			g_direct_hash, g_direct_equal,
			NULL, (GDestroyNotify) ccl_wrapper_info_destroy);
	}
	
	/* Keep information in information table. */
	g_hash_table_replace(wrapper->info,
		GUINT_TO_POINTER(param_name), info);
}

/**
 * Get information about any wrapped OpenCL object.
 * 
 * This function should not be called directly, but using the
 * ccl_*_info() macros instead.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper1 The wrapper object to query.
 * @param[in] wrapper2 A second wrapper object, required in some 
 * queries.
 * @param[in] param_name Name of information/parameter to get.
 * @param[in] info_fun OpenCL clGet*Info function.
 * @param[in] use_cache TRUE if cached information is to be used, FALSE 
 * to force a new query even if information is in cache.
 * @param[out] err Return location for a GError, or NULL if error 
 * reporting is to be ignored.
 * @return The requested information object. This object will
 * be automatically freed when the respective wrapper object is 
 * destroyed. If an error occurs, NULL is returned.
 * */
CCLWrapperInfo* ccl_wrapper_get_info(CCLWrapper* wrapper1, 
	CCLWrapper* wrapper2, cl_uint param_name, 
	ccl_wrapper_info_fp info_fun, cl_bool use_cache, GError** err) {

	/* Make sure err is NULL or it is not set. */
	g_return_val_if_fail((err) == NULL || *(err) == NULL, NULL);
	
	/* Make sure wrapper1 is not NULL. */
	g_return_val_if_fail(wrapper1 != NULL, NULL);
	
	/* Information object. */
	CCLWrapperInfo* info = NULL;
	
	/* Check if it is required to query OpenCL object. */
	if ((!use_cache) /* Query it if info table cache is not to be used. */
		/* Do query if info table is not yet initialized. */
		|| (wrapper1->info == NULL)
		/* Do query if info table does not contain requested info.  */
		|| (!g_hash_table_contains(
				wrapper1->info, GUINT_TO_POINTER(param_name)))
		) {

		/* Let's query OpenCL object.*/
		cl_int ocl_status;
		/* Size of device information in bytes. */
		size_t size_ret = 0;
		
		/* Get size of information. */
		ocl_status = (wrapper2 == NULL)
			? ((ccl_wrapper_info_fp1) info_fun)(wrapper1->cl_object, 
				param_name, 0, NULL, &size_ret)
			: ((ccl_wrapper_info_fp2) info_fun)(wrapper1->cl_object, 
				wrapper2->cl_object, param_name, 0, NULL, &size_ret);
		gef_if_err_create_goto(*err, CCL_ERROR,
			CL_SUCCESS != ocl_status, CCL_ERROR_OCL, error_handler,
			"%s: get info [size] (OpenCL error %d: %s).",
			G_STRLOC, ocl_status, ccl_err(ocl_status));
		gef_if_err_create_goto(*err, CCL_ERROR,
			size_ret == 0, CCL_ERROR_OCL, error_handler,
			"%s: get info [size] (size is 0).",
			G_STRLOC);
		
		/* Allocate memory for information. */
		info = ccl_wrapper_info_new(size_ret);
		
		/* Get information. */
		ocl_status = (wrapper2 == NULL)
			? ((ccl_wrapper_info_fp1) info_fun)(wrapper1->cl_object, 
				param_name, size_ret, info->value, NULL)
			: ((ccl_wrapper_info_fp2) info_fun)(wrapper1->cl_object, 
				wrapper2->cl_object, param_name, size_ret, info->value, 
				NULL);
		gef_if_err_create_goto(*err, CCL_ERROR,
			CL_SUCCESS != ocl_status, CCL_ERROR_OCL, error_handler,
			"%s: get context info [info] (OpenCL error %d: %s).",
			G_STRLOC, ocl_status, ccl_err(ocl_status));
			
		/* Keep information in information table. */
		ccl_wrapper_add_info(wrapper1, param_name, info);

	} else {

		/* Requested infor is already present in the info table,
		 * retrieve it from there. */
		info = g_hash_table_lookup(
			wrapper1->info, GUINT_TO_POINTER(param_name));
		
	}
	
	/* If we got here, everything is OK. */
	g_assert(err == NULL || *err == NULL);
	goto finish;
	
error_handler:
	/* If we got here there was an error, verify that it is so. */
	g_assert(err == NULL || *err != NULL);
	info = NULL;
	
finish:
	
	/* Return the requested information. */
	return info;
}

/** 
 * Get pointer to information value.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper1 The wrapper object to query.
 * @param[in] wrapper2 A second wrapper object, required in some 
 * queries.
 * @param[in] param_name Name of information/parameter to get value of.
 * @param[in] info_fun OpenCL clGet*Info function.
 * @param[in] use_cache TRUE if cached information is to be used, FALSE
 * to force a new query even if information is in cache.
 * @param[out] err Return location for a GError, or NULL if error
 * reporting is to be ignored.
 * @return A pointer to the requested information value. This 
 * value will be automatically freed when the wrapper object is 
 * destroyed. If an error occurs, NULL is returned.
 * */
void* ccl_wrapper_get_info_value(CCLWrapper* wrapper1, 
	CCLWrapper* wrapper2, cl_uint param_name, 
	ccl_wrapper_info_fp info_fun, cl_bool use_cache, GError** err) {

	/* Make sure err is NULL or it is not set. */
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);

	/* Make sure wrapper1 is not NULL. */
	g_return_val_if_fail(wrapper1 != NULL, NULL);
	
	/* Get information object. */
	CCLWrapperInfo* diw = ccl_wrapper_get_info(wrapper1, wrapper2, 
		param_name, info_fun, use_cache, err);
	
	/* Return value if information object is not NULL. */	
	return diw != NULL ? diw->value : NULL;
}

/** 
 * Get information size.
 * 
 * @protected @memberof ccl_wrapper
 * 
 * @param[in] wrapper1 The wrapper object to query.
 * @param[in] wrapper2 A second wrapper object, required in some
 * queries.
 * @param[in] param_name Name of information/parameter to get value of.
 * @param[in] info_fun OpenCL clGet*Info function.
 * @param[in] use_cache TRUE if cached information is to be used, FALSE
 * to force a new query even if information is in cache.
 * @param[out] err Return location for a GError, or NULL if error
 * reporting is to be ignored.
 * @return The requested information size. If an error occurs, 
 * a size of 0 is returned.
 * */
size_t ccl_wrapper_get_info_size(CCLWrapper* wrapper1, 
	CCLWrapper* wrapper2, cl_uint param_name, 
	ccl_wrapper_info_fp info_fun, cl_bool use_cache, GError** err) {
	
	/* Make sure err is NULL or it is not set. */
	g_return_val_if_fail(err == NULL || *err == NULL, 0);

	/* Make sure wrapper1 is not NULL. */
	g_return_val_if_fail(wrapper1 != NULL, 0);
	
	/* Get information object. */
	CCLWrapperInfo* diw = ccl_wrapper_get_info(wrapper1, wrapper2, 
		param_name, info_fun, use_cache, err);
	
	/* Return value if information object is not NULL. */	
	return diw != NULL ? diw->size : 0;
}

/** 
 * Debug function which checks if memory allocated by wrappers
 * has been properly freed.
 * 
 * @public @memberof ccl_wrapper
 * 
 * This function is merely a debug helper and shouldn't replace
 * proper leak checks with Valgrind or similar tool.
 * 
 * @return CL_TRUE if memory allocated by wrappers has been properly
 * freed, CL_FALSE otherwise.
 */
cl_bool ccl_wrapper_memcheck() {
	return wrappers == NULL;
}

/**
 * @addtogroup WRAPPER_INFO
 *  
 * @{
 */
 
/**
 * Create a new ::CCLWrapperInfo object with a given value size.
 * 
 * @public @memberof ccl_wrapper_info
 * 
 * @param[in] size Parameter size in bytes.
 * @return A new CCLWrapperInfo* object.
 * */
CCLWrapperInfo* ccl_wrapper_info_new(size_t size) {
	
	CCLWrapperInfo* info = g_slice_new(CCLWrapperInfo);
	
	if (size > 0) 
		info->value = g_slice_alloc0(size);
	else
		info->value = NULL;
	info->size = size;
	
	return info;
	
}

/**
 * Destroy a ::CCLWrapperInfo object.
 * 
 * @public @memberof ccl_wrapper_info
 * 
 * @param[in] info Object to destroy.
 * */
void ccl_wrapper_info_destroy(CCLWrapperInfo* info) {
		
	/* Make sure info is not NULL. */
	g_return_if_fail(info != NULL);

	if (info->size > 0)
		g_slice_free1(info->size, info->value);
	g_slice_free(CCLWrapperInfo, info);
	
}

/** @} */
