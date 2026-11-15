/* TrueType (MiSans/MiSans Global) and CFF (Inter), with the smooth and
 * monochrome rasterizers.  Everything else in FreeType's default module list
 * is omitted to keep the freestanding image small.
 *
 * This header is included twice by ftinit.c (declarations and the module
 * array), so it must not have an include guard. */
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
