use std::ptr;
unsafe extern "C" {fn ores_iot_required_capacity(kind:i32, payload_len:usize,required:*mut usize)->i32;}
#[test] fn frame_capacity(){for kind in 1..=3 {for n in [0,1,1023,1024]{let mut got=usize::MAX;assert_eq!(unsafe{ores_iot_required_capacity(kind,n,&mut got)},0);assert_eq!(got,8+n);}}let mut got=77;assert_eq!(unsafe{ores_iot_required_capacity(1,1025,&mut got)},2);assert_eq!(got,0);got=77;assert_eq!(unsafe{ores_iot_required_capacity(0,1,&mut got)},6);assert_eq!(got,0);assert_eq!(unsafe{ores_iot_required_capacity(1,0,ptr::null_mut())},1);}
